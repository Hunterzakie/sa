#!/usr/bin/env python3
"""Release date of the version being built, for the AppStream metadata.

This mirrors VERSION_DATE in configure.ac: the date written to .version-date
in a release tarball, else the date of the annotated version tag pointing at
HEAD, else today's date.

Release tarballs carry no git history, so the date is written to their
.version-date when they are made: by the dist-hook of Makefile.am for
autotools, and by the --dist mode of this script for "meson dist".
"""
import argparse
import datetime
import os
import re
import subprocess
import sys

VERSION_PATTERN = re.compile(r'^v?([0-9]+\.?)+(-[0-9]+)?$')

# Where release tarballs carry their date, read by configure.ac too
VERSION_DATE_FILE = '.version-date'


def git(srcdir, *args):
    try:
        result = subprocess.run(['git', '-C', srcdir, *args],
                                capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return ''
    return result.stdout


def date_from_tag(srcdir):
    """Date of the annotated version tag pointing at HEAD, if any."""
    # git also finds the repositories enclosing srcdir, whose tags are
    # unrelated: only look at a repository rooted at srcdir
    toplevel = git(srcdir, 'rev-parse', '--show-toplevel').strip()
    if not toplevel or not os.path.samefile(toplevel, srcdir):
        return None

    tags = git(srcdir, 'tag', '--points-at', 'HEAD',
               '--sort=-version:refname').splitlines()
    tags = [tag for tag in tags if VERSION_PATTERN.match(tag)]
    if not tags:
        return None
    date = git(srcdir, 'for-each-ref', '--format=%(taggerdate:short)',
               'refs/tags/' + tags[0]).strip()
    if not date and os.environ.get('TAG'):
        sys.exit('Building with TAG envvar set but no date found on the tag, '
                 'please check that your git tag is annotated')
    return date or None


def frozen_date(srcdir):
    """Date written by the release tarball creation, if any."""
    path = os.path.join(srcdir, VERSION_DATE_FILE)
    try:
        with open(path, encoding='utf-8') as f:
            return f.read().strip()
    except FileNotFoundError:
        return None


def version_date(srcdir):
    return (frozen_date(srcdir)
            or date_from_tag(srcdir)
            or datetime.date.today().isoformat())


def main():
    parser = argparse.ArgumentParser(
        description='Print the release date of the version being built')
    parser.add_argument('srcdir', nargs='?', default='.',
                        help='source tree to get the date for (default: .)')
    parser.add_argument('--dist', action='store_true',
                        help='write the date to the tarball being created by '
                             '"meson dist" ($MESON_PROJECT_DIST_ROOT) instead '
                             'of printing it')
    args = parser.parse_args()

    date = version_date(args.srcdir)
    if not args.dist:
        print(date)
        return

    distdir = os.environ.get('MESON_PROJECT_DIST_ROOT')
    if not distdir:
        sys.exit('--dist: MESON_PROJECT_DIST_ROOT is not set, '
                 'this is meant to be run by "meson dist"')
    with open(os.path.join(distdir, VERSION_DATE_FILE), 'w',
              encoding='utf-8') as f:
        f.write(date + '\n')


if __name__ == '__main__':
    main()
