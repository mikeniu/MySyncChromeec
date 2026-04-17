#!/usr/bin/env python3
import argparse
import subprocess
import os
import re
import sys
import logging
import csv
from datetime import datetime
from gcs_download import get_major_from_branch, get_latest_version, download_firmware

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')

def normalize_path(path):
    """Normalize a file path from ELF/MAP to a repository-relative path."""
    path = re.sub(r'\.(o|obj|c\.obj)$', '.c', path)
    path = re.sub(r'^.*build/[^/]+/', '', path)
    path = re.sub(r'^zephyr/', '', path)
    if '/platform/ec/' in path:
        path = path.split('/platform/ec/')[-1]
    path = path.lstrip('./')
    while path.startswith('../'):
        path = path[3:]
    return path

def run_command(cmd, cwd=None, shell=False):
    """Utility to run shell commands."""
    try:
        result = subprocess.run(cmd, cwd=cwd, shell=shell, check=True,
                             capture_output=True, text=True, errors='replace')
        return result.stdout.strip()
    except subprocess.CalledProcessError:
        return None

def get_compiled_files(extract_dir):
    """Extract compiled files from map files or ELF symbols."""
    files = set()
    map_files = []
    for root, _, filenames in os.walk(extract_dir):
        for filename in filenames:
            if filename.endswith('.map') or filename.endswith('.smap'):
                map_files.append(os.path.join(root, filename))
    
    if map_files:
        for map_path in map_files:
            with open(map_path, 'r') as f:
                for line in f:
                    match = re.search(r'([a-zA-Z0-9_\-/]+\.(o|obj))', line)
                    if match:
                        files.add(normalize_path(match.group(1)))
        if files: return files

    # Fallback to ELF strings
    elf_targets = ['ec.RO.elf', 'ec.RW.elf', 'zephyr.ro.elf', 'zephyr.rw.elf']
    for elf in elf_targets:
        elf_path = os.path.join(extract_dir, elf)
        if os.path.exists(elf_path):
            output = run_command(['strings', elf_path])
            if output:
                for line in output.splitlines():
                    if line.endswith('.o') or line.endswith('.c') or '.c.obj' in line:
                        files.add(normalize_path(line))
    return files

def get_elf_symbols(extract_dir):
    """Extract function symbols from any ELFs found in extract_dir."""
    symbols = set()
    elf_targets = ['ec.RO.elf', 'ec.RW.elf', 'zephyr.ro.elf', 'zephyr.rw.elf']
    for elf in elf_targets:
        elf_path = os.path.join(extract_dir, elf)
        if os.path.exists(elf_path):
            output = run_command(['objdump', '-t', elf_path])
            if output:
                for line in output.splitlines():
                    if ' F ' in line:
                        parts = line.split()
                        if parts: symbols.add(parts[-1])
    return symbols

def get_commit_info(commit):
    """Get subject, files and symbols for a commit."""
    subject = run_command(['git', 'log', '-1', '--format=%s', commit])
    
    files = run_command(['git', 'show', '--pretty=', '--name-only', commit])
    file_list = [f.strip() for f in files.splitlines() if f.strip()] if files else []
    
    symbols = set()
    diff = run_command(['git', 'show', '--pretty=', commit])
    if diff:
        for line in diff.splitlines():
            if line.startswith('@@'):
                match = re.search(r'@@.*@@\s*([a-zA-Z_][a-zA-Z0-9_]*)', line)
                if match:
                    symbol = match.group(1)
                    if symbol not in ['if', 'for', 'while', 'switch', 'return']:
                        symbols.add(symbol)
    
    return {
        'subject': subject or "",
        'files': file_list,
        'symbols': symbols
    }

def get_bugs_from_log(branch, since_commit=None):
    """Get Bug IDs from git log of a branch."""
    range_spec = branch
    if since_commit:
        range_spec = f"{since_commit}..{branch}"
    
    cmd = ['git', 'log', range_spec, '--grep=BUG=', '--grep=Fixed:', '--grep=b/']
    log = run_command(cmd)
    if not log: return {}
    
    bug_to_commit = {}
    current_commit = None
    for line in log.splitlines():
        commit_match = re.match(r'^commit ([a-f0-9]+)', line)
        if commit_match:
            current_commit = commit_match.group(1)
            continue
        
        bug_match = re.search(r'(BUG=(b:|chromium:)?|Fixed:\s*|b/)\s*(\d+)', line)
        if bug_match and current_commit:
            bug_id = bug_match.group(3)
            if bug_id not in bug_to_commit:
                bug_to_commit[bug_id] = []
            if current_commit not in bug_to_commit[bug_id]:
                bug_to_commit[bug_id].append(current_commit)
            
    return bug_to_commit

def get_patch_id(commit):
    """Get git patch-id for a commit."""
    diff = subprocess.Popen(['git', 'show', commit], stdout=subprocess.PIPE)
    patch_id_proc = subprocess.Popen(['git', 'patch-id'], stdin=diff.stdout, stdout=subprocess.PIPE, text=True)
    diff.stdout.close()
    out, _ = patch_id_proc.communicate()
    return out.split()[0] if out else None

def get_branch_patch_ids(branch):
    """Cache patch-ids for all commits in a branch."""
    commits = run_command(['git', 'rev-list', branch])
    patch_ids = {}
    if commits:
        for commit in commits.splitlines():
            pid = get_patch_id(commit)
            if pid: patch_ids[pid] = commit
    return patch_ids

def check_fix_present(commit):
    """Check if a fix from main is already present using git apply check."""
    patch = run_command(['git', 'show', commit])
    if not patch: return False
    try:
        process = subprocess.Popen(['git', 'apply', '--reverse', '--check', '-'], 
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, 
                                   stderr=subprocess.PIPE, text=True)
        process.communicate(input=patch)
        return process.returncode == 0
    except Exception:
        return False

def get_labels(bug_id, commit_info):
    """Heuristic labeling of bugs based on commit metadata."""
    labels = []
    subject = commit_info['subject'].lower()
    files = [f.lower() for f in commit_info['files']]
    
    # Label: bringup
    # Catch phrases like "bringup", "initial", "add board", or board-specific paths in non-common areas
    if any(x in subject for x in ['bringup', 'initial', 'quirk', 'wdt disable']):
        labels.append('bringup')
    
    # Label: bugfix
    if any(x in subject for x in ['fix', 'bug', 'correct', 'prevent', 'resolve']):
        labels.append('bugfix')
    
    # Label: power
    if any(x in subject for x in ['power', 'pmic', 'battery', 'charger', 'charge', 'vbus']):
        labels.append('power')
    if any('power' in f or 'battery' in f or 'pmic' in f for f in files):
        labels.append('power')

    # Label: security
    if any(x in subject for x in ['security', 'cve', 'vulnerability', 'exploit']):
        labels.append('security')

    # Label: stability
    if any(x in subject for x in ['crash', 'panic', 'hang', 'watchdog', 'wdt', 'assert']):
        labels.append('stability')

    # Label: thermal
    if any(x in subject for x in ['thermal', 'temp', 'dptf', 'fan', 'throttle']):
        labels.append('thermal')
    
    # Label: feature
    if any(x in subject for x in ['add feature', 'new feature', 'support for']):
        labels.append('feature')

    if not labels:
        labels.append('unclassified')
        
    return list(set(labels))

def main():
    parser = argparse.ArgumentParser(description='Highlander Parity Auditor (Per-Bug Report)')
    parser.add_argument('board', help='Board name to audit')
    parser.add_argument('--main', default='remotes/cros/main', help='Main branch reference')
    parser.add_argument('--verify', action='store_true', help='Perform source-level verification')
    parser.add_argument('--output', default='bug_parity_results.csv', help='Output CSV file')
    args = parser.parse_args()

    board = args.board
    main_branch = args.main
    legacy_branch = run_command(['git', 'branch', '-r', '--list', f'*/firmware-{board}-*.B'])
    if not legacy_branch:
         # Fallback search
         branches = run_command(['git', 'branch', '-r'])
         matches = [b.strip() for b in branches.splitlines() if f'firmware-{board}-' in b and b.strip().endswith('.B')]
         if matches: legacy_branch = sorted(matches)[-1]
         else: sys.exit(f"No legacy branch found for {board}")
    else:
         legacy_branch = legacy_branch.splitlines()[-1].strip()
        
    logging.info(f"Auditing {board}: {legacy_branch} vs {main_branch}")
    
    current_branch = run_command(['git', 'rev-parse', '--abbrev-ref', 'HEAD'])
    common_ancestor = run_command(['git', 'merge-base', main_branch, legacy_branch])
    
    main_bugs = get_bugs_from_log(main_branch, since_commit=common_ancestor)
    branch_bugs = get_bugs_from_log(legacy_branch)
    
    logging.info("Caching legacy branch state...")
    branch_patch_ids = get_branch_patch_ids(legacy_branch)

    # model -> { 'files': set(), 'symbols': set() }
    model_data = {}
    
    try:
        run_command(['git', 'checkout', legacy_branch])
        is_zephyr = os.path.exists(f"zephyr/program/{board}")
        
        # Simple model discovery
        models = [board]
        if is_zephyr:
             build_py = os.path.join('zephyr', 'program', board, 'BUILD.py')
             if os.path.exists(build_py):
                 with open(build_py, 'r') as f:
                     matches = re.findall(r'project_name=["\']([^"\']+)["\']', f.read())
                     if matches: models = sorted(list(set(matches)))
        
        for m in models:
            model_data[m] = {'files': set(), 'symbols': set()}
            major = get_major_from_branch(legacy_branch)
            if major:
                version = get_latest_version(board, major)
                if version:
                    extract_dir = download_firmware(board, m, version, bucket_major=major)
                    if extract_dir:
                        model_data[m]['files'] = get_compiled_files(extract_dir)
                        model_data[m]['symbols'] = get_elf_symbols(extract_dir)

        # Bug Report Data: bug_id -> model -> {status, labels, subject}
        bug_report = {}

        for bug_id, commits in main_bugs.items():
            commit = commits[0] # Use primary commit for metadata
            info = get_commit_info(commit)
            labels = get_labels(bug_id, info)
            
            bug_report[bug_id] = {
                'subject': info['subject'],
                'labels': labels,
                'models': {}
            }
            
            # Check if included globally via BUG ID or Patch ID
            globally_included = (bug_id in branch_bugs)
            if not globally_included:
                for c in commits:
                    if get_patch_id(c) in branch_patch_ids:
                        globally_included = True; break
            
            # Source verification (optional)
            if not globally_included and args.verify:
                for c in commits:
                    if check_fix_present(c):
                        globally_included = True; break

            for m in models:
                status = "Missing"
                if globally_included:
                    status = "Included"
                else:
                    # Relevance check
                    relevant = False
                    comp_files = model_data[m]['files']
                    comp_symbols = model_data[m]['symbols']
                    
                    if comp_symbols:
                        for c in commits:
                             c_info = get_commit_info(c)
                             if any(s in comp_symbols for s in c_info['symbols']):
                                 relevant = True; break
                    
                    if not relevant and comp_files:
                        for c in commits:
                             c_info = get_commit_info(c)
                             if any(normalize_path(f) in comp_files for f in c_info['files']):
                                 relevant = True; break
                    
                    if not relevant:
                         status = "Not Relevant"
                
                bug_report[bug_id]['models'][m] = status

        # Save Per-Bug CSV
        fieldnames = ['bug_id', 'subject', 'labels', 'board'] + models
        with open(args.output, 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            for bug_id, data in bug_report.items():
                row = {
                    'bug_id': bug_id,
                    'subject': data['subject'],
                    'labels': ','.join(data['labels']),
                    'board': board
                }
                row.update(data['models'])
                writer.writerow(row)
        
        logging.info(f"Per-bug report saved to {args.output}")

    finally:
        run_command(['git', 'checkout', current_branch])

if __name__ == "__main__":
    main()
