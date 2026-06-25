import os
from pathlib import Path
import sys
import argparse
import glob
import subprocess

def collectJson(jsondir, output):
    path = str(Path(jsondir).resolve())

    # https://stackoverflow.com/questions/18394147/how-to-do-a-recursive-sub-folder-search-and-return-files-in-a-list:
    files = glob.glob(path + '/**/*.moc*.json', recursive=True)

    moc = ["moc", "--collect-json", "-o", output, *files]

    result = subprocess.run(moc, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"moc --collect-json failed with the output:" + output + ", and recursively found input files:", files)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='This program collects json through moc. Input directory is recursively searched for .moc.*.json files. If input directory is not specified, the current directory is used.'
    )
    parser.add_argument("-o", "--output", help="output collected json file, if empty 'metatypes.moc.json' is used.")
    parser.add_argument("--metatypejsondir", help="directory that will be recursively searched for collecting meta type info. "
                                                  "json files must be ending with .moc.*.json, and the output file will be collected.moc.json")
    args = parser.parse_args()

    output = args.output
    metatypejsondir = args.metatypejsondir

    if not args.output:
        output = "metatypes.moc.json"

    if not args.metatypejsondir:
        args.metatypejsondir = "."


    #ensure outdir exists
    os.makedirs(os.path.dirname(args.output), exist_ok=True)

    collectJson(args.metatypejsondir, args.output)

