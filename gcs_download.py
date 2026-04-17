#!/usr/bin/env python3
import subprocess
import os
import re
import logging
import tarfile

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')

FW_NAME_QUIRKS = {
    "trulo": ["orisa", "pujjocento", "pujjogatwin", "pujjolo", "trulo", "uldrenite"],
    "kukui": [
        "burnet",
        "cerise",
        "damu",
        "esche",
        "fennel",
        "fennel14",
        "jacuzzi",
        "juniper",
        "kappa",
        "stern",
        "willow",
    ],
    "brya": [
        "aurash",
        "brask",
        "bujia",
        "gaelin",
        "gladios",
        "kinox",
        "kuldax",
        "kulnex",
        "lisbon",
        "moli",
        "moxie",
    ],
}

def run_command(cmd, cwd=None, shell=False):
    """Utility to run shell commands."""
    try:
        # Prefer gcloud storage over gsutil if available
        if cmd[0] == 'gsutil':
            new_cmd = ['gcloud', 'storage'] + cmd[1:]
            logging.debug(f"Translating gsutil to gcloud storage: {' '.join(new_cmd)}")
            cmd = new_cmd
            
        result = subprocess.run(cmd, cwd=cwd, shell=shell, check=True,
                             capture_output=True, text=True, errors='replace')
        return result.stdout.strip()
    except subprocess.CalledProcessError as e:
        logging.error(f"Command failed: {' '.join(cmd) if isinstance(cmd, list) else cmd}")
        if e.stderr:
            logging.error(f"Error output: {e.stderr.strip()}")
        return None

def get_major_from_branch(branch_name):
    """Extract major version from branch name (e.g. firmware-board-12345.B -> 12345)."""
    logging.info(f"Extracting major version from branch: {branch_name}")
    # Pattern: firmware-<anything>-<version>.B
    # We want the <version> part which might contain dots.
    match = re.search(r'firmware-.*-([\d\.]+)\.B', branch_name)
    if match:
        major = match.group(1)
        logging.info(f"Detected major version: {major}")
        return major
        
    logging.warning(f"Could not extract major version from {branch_name}")
    return None

def get_latest_version(board, major_version):
    """Find the latest version on GCS for a given board and major version."""
    gcs_base = f"gs://firmware-image-archive/firmware-{board}-{major_version}.B"
    logging.info(f"Searching for versions in {gcs_base}...")
    output = run_command(['gsutil', 'ls', f"{gcs_base}/"])
    if output is None:
        logging.error(f"gsutil command failed or returned no output for {gcs_base}")
        return None
    
    versions = []
    for line in output.splitlines():
        # Match something like gs://.../10139.232.0/
        match = re.search(rf'/({major_version}\.[\d\.]+)/?$', line)
        if match:
            versions.append(match.group(1).strip('/'))
    
    if not versions:
        logging.warning(f"No versions found matching {major_version}.* in {gcs_base}")
        return None
    
    def version_key(v):
        try:
            return [int(x) for x in v.split('.')]
        except ValueError:
            return [0]
            
    latest = sorted(versions, key=version_key)[-1]
    logging.info(f"Latest version found: {latest}")
    return latest

def get_quirked_board(name):
    """Returns the quirked board name if it exists."""
    for quirk_board, names in FW_NAME_QUIRKS.items():
        if name in names:
            return quirk_board
    return None

def download_firmware(board, model, version, bucket_major=None):
    """Download and extract firmware from GCS."""
    if bucket_major is None:
        # Fallback: try to derive from version
        parts = version.split('.')
        if len(parts) > 2:
            # If 3+ parts, try the first two (e.g. 15217.771)
            bucket_major = f"{parts[0]}.{parts[1]}"
        else:
            bucket_major = parts[0]
            
    # Strategy 1: Primary (board) and Secondary (model)
    prefixes = [board, model]
    
    # Strategy 3: Quirked board mapping
    quirked = get_quirked_board(model) or get_quirked_board(board)
    if quirked and quirked not in prefixes:
        prefixes.append(quirked)
    
    extract_dir = f"build/gcs/{model}/{version}"
    if os.path.exists(extract_dir) and os.listdir(extract_dir):
        logging.info(f"Using cached firmware in {extract_dir}")
        return extract_dir
        
    os.makedirs(extract_dir, exist_ok=True)
    local_tar = os.path.join(extract_dir, "artifact.tar.bz2")
    
    success = False
    # Try with the provided bucket_major first
    candidate_majors = [bucket_major]
    # If bucket_major had dots, also try the first part as a fallback
    if '.' in bucket_major:
        candidate_majors.append(bucket_major.split('.')[0])
        
    for major in candidate_majors:
        for prefix in prefixes:
            gcs_path = f"gs://firmware-image-archive/firmware-{board}-{major}.B/{version}/{prefix}.EC_elf.{version}.tar.bz2"
            logging.info(f"Attempting GCS download: {gcs_path}")
            res = run_command(['gcloud', 'storage', 'cp', gcs_path, local_tar])
            if res is not None:
                success = True
                break
        if success:
            break
            
    # Strategy 4: Brute-force search for any tar.bz2 in the directory
    if not success:
        logging.info("Attempting brute-force search for any firmware artifact...")
        for major in candidate_majors:
            gcs_dir = f"gs://firmware-image-archive/firmware-{board}-{major}.B/{version}/"
            ls_out = run_command(['gcloud', 'storage', 'ls', f"{gcs_dir}*.tar.bz2"])
            if ls_out:
                # Pick the first one
                gcs_path = ls_out.splitlines()[0]
                logging.info(f"Brute-force found candidate: {gcs_path}")
                res = run_command(['gcloud', 'storage', 'cp', gcs_path, local_tar])
                if res is not None:
                    success = True
                    break

    if success:
        try:
            logging.info(f"Extracting {local_tar}...")
            with tarfile.open(local_tar, 'r:bz2') as tar:
                tar.extractall(path=extract_dir)
            os.remove(local_tar)
            return extract_dir
        except Exception as e:
            logging.error(f"Extraction error for {local_tar}: {e}")
                
    logging.error(f"Failed to download firmware for {model} version {version}")
    try:
        if not os.listdir(extract_dir):
            os.rmdir(extract_dir)
    except OSError:
        pass
    return None

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description='GCS Firmware Downloader Test')
    parser.add_argument('--board', required=True, help='Board name')
    parser.add_argument('--model', required=True, help='Model name')
    parser.add_argument('--version', help='Specific version (optional)')
    parser.add_argument('--branch', help='Branch name to derive version from')
    
    args = parser.parse_args()
    
    version = args.version
    major = None
    if not version and args.branch:
        major = get_major_from_branch(args.branch)
        if major:
            version = get_latest_version(args.board, major)
            
    if not version:
        logging.error("Version could not be determined. Provide --version or --branch and --board.")
        exit(1)
        
    res = download_firmware(args.board, args.model, version, bucket_major=major)
    if res:
        print(f"SUCCESS: Firmware downloaded to {res}")
    else:
        print("FAILURE: Could not download firmware")
