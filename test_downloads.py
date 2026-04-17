#!/usr/bin/env python3
import subprocess
import logging

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')

TEST_CASES = {
    "trulo": {
        "branch": "cros/firmware-trulo-15217.771.B-main",
        "models": ["orisa", "pujjocento", "pujjogatwin", "pujjolo", "trulo", "uldrenite"]
    },
    "kukui": {
        "branch": "cros/firmware-kukui-12573.B",
        "models": ["burnet", "cerise", "damu", "esche", "fennel", "fennel14", "jacuzzi", "juniper", "kappa", "stern", "willow"]
    },
    "brya": {
        "branch": "cros/firmware-brya-14505.B-main",
        "models": ["aurash", "brask", "bujia", "gaelin", "gladios", "kinox", "kuldax", "kulnex", "lisbon", "moli", "moxie"]
    },
    "dedede": {
        "branch": "cros/firmware-dedede-13606.B-master",
        "models": ["awasuki", "beadrix", "beetley", "blipper", "boten", "boxy", "bugzzy", "corori2", "cret", "dexi", "dibbi", "dita", "drawcia", "galtic", "kracko", "lantis", "madoo", "magolor", "metaknight", "pirika", "sasuke", "sasukette", "shotzo", "storo", "taranza", "waddledee", "waddledoo", "wheelie"]
    },
    "fizz": {
        "branch": "cros/firmware-fizz-10139.B",
        "models": ["fizz"]
    },
    "octopus": {
        "branch": "cros/firmware-octopus-11297.B",
        "models": ["ampton", "bloog", "bobba", "casta", "dood", "fleex", "foob", "garg", "lick", "meep", "phaser", "yorp"]
    },
    "zork": {
        "branch": "cros/firmware-zork-13434.B-master",
        "models": ["berknip", "dalboz", "dirinboz", "ezkinil", "gumboz", "morphius", "shuboz", "trembyle", "vilboz", "woomax"]
    }
}

def run_test(board, model, branch):
    cmd = ["python3", "gcs_download.py", "--board", board, "--model", model, "--branch", branch]
    logging.info(f"Testing {board}/{model} with branch {branch}...")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if "SUCCESS" in result.stdout:
            return True, result.stdout
        else:
            return False, result.stderr + result.stdout
    except Exception as e:
        return False, str(e)

def main():
    results = []
    # Test only the newly requested boards to save time
    new_boards = ["dedede", "fizz", "octopus", "zork"]
    
    for board in new_boards:
        data = TEST_CASES[board]
        branch = data["branch"]
        for model in data["models"]:
            success, output = run_test(board, model, branch)
            results.append((board, model, success))
            if success:
                print(f"[PASS] {board}/{model}")
            else:
                print(f"[FAIL] {board}/{model}")

    print("\nSummary (New Boards):")
    print(f"{'Board':<10} {'Model':<15} {'Status':<10}")
    print("-" * 35)
    for board, model, success in results:
        status = "PASS" if success else "FAIL"
        print(f"{board:<10} {model:<15} {status:<10}")

if __name__ == "__main__":
    main()
