#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
 Convert the ASCII download_file_types.asciipb proto into a binary resource.

 We generate a separate variant of the binary proto for each platform,
 each which contains only the values that platform needs.
"""

import optparse
import re
import subprocess
import sys


def ImportProtoModules(paths):
  """Import the protobuf modiles we need. |paths| is list of import paths"""
  for path in paths:
    # Put the path to our proto libraries in front, so that we don't use system
    # protobuf.
    sys.path.insert(1, path)

  import download_file_types_pb2 as config_pb2
  globals()['config_pb2'] = config_pb2

  import google.protobuf.text_format as text_format
  globals()['text_format'] = text_format


# Map of platforms for which we can generate binary protos.
# This must be run after the custom imports.
#   key: type-name
#   value: proto-platform_type (int)
def PlatformTypes():
  return {
    "android": config_pb2.DownloadFileType.PLATFORM_ANDROID,
    "chromeos": config_pb2.DownloadFileType.PLATFORM_CHROME_OS,
    "linux": config_pb2.DownloadFileType.PLATFORM_LINUX,
    "mac": config_pb2.DownloadFileType.PLATFORM_MAC,
    "win": config_pb2.DownloadFileType.PLATFORM_WINDOWS,
  }


def ValidatePb(pb):
  """ Validate the basic values of the protobuf.  The
      file_type_policies_unittest.cc will also validate it by platform,
      but this will catch errors earlier.
  """
  assert pb.version_id > 0;
  assert pb.sampled_ping_probability >= 0.0;
  assert pb.sampled_ping_probability <= 1.0;
  assert len(pb.default_file_type.platform_settings) >= 1;
  assert len(pb.file_types) > 1;


def PrunePlatformSettings(file_type, default_settings, platform_type):
  # Modify this file_type's platform_settings by keeping the only the
  # best one for this platform_type. In order of preference:
  #   * Exact match to platform_type
  #   * PLATFORM_ANY entry
  #   * or copy from the default file type.

  last_platform = -1
  setting_match = None
  for s in file_type.platform_settings:
    # Enforce: sorted and no dups (signs of mistakes).
    assert last_platform < s.platform, (
        "Extension '%s' has duplicate or out of order platform: '%s'" %
        (file_type.extension, s.platform))
    last_platform = s.platform

    # Pick the most specific match.
    if ((s.platform == platform_type) or
        (s.platform == config_pb2.DownloadFileType.PLATFORM_ANY and
         setting_match is None)):
      setting_match = s

  # If platform_settings was empty, we'll fill in from the default
  if setting_match is None:
    assert default_settings is not None, (
        "Missing default settings for platform %d" % platform_type)
    setting_match = default_settings

  # Now clear out the full list and replace it with 1 entry.
  del file_type.platform_settings[:]
  new_setting = file_type.platform_settings.add()
  new_setting.CopyFrom(setting_match)
  new_setting.ClearField('platform')


def FilterPbForPlatform(full_pb, platform_type):
  """ Return a filtered protobuf for this platform_type """

  new_pb = config_pb2.DownloadFileTypeConfig();
  new_pb.CopyFrom(full_pb)

  # Ensure there's only one platform_settings for the default.
  PrunePlatformSettings(new_pb.default_file_type, None, platform_type)

  # This can be extended if we want to match weird extensions.
  # Just no dots, non-UTF8, or uppercase chars.
  invalid_char_re = re.compile('[^a-z0-9_-]')

  # Filter platform_settings for each type.
  uma_values_used = set()
  extensions_used = set()
  for file_type in new_pb.file_types:
    assert not invalid_char_re.search(file_type.extension), (
        "File extension '%s' contains non alpha-num-dash chars" % (
            file_type.extension))
    assert file_type.extension is not extensions_used, (
        "Duplicate extension '%s'" % file_type.extension)
    extensions_used.add(file_type.extension)

    assert file_type.uma_value not in uma_values_used, (
        "Extension '%s' reused UMA value %d." % (
            file_type.extension, file_type.uma_value))
    uma_values_used.add(file_type.uma_value)

    # Modify file_type to include only the best match platform_setting.
    PrunePlatformSettings(
        file_type, new_pb.default_file_type.platform_settings[0], platform_type)

  return new_pb


def GeneratBinaryProtos(opts):
  # Read the ASCII
  ifile = open(opts.infile, 'r')
  ascii_pb_str = ifile.read()
  ifile.close()

  # Parse it into a structure PB
  pb = config_pb2.DownloadFileTypeConfig()
  text_format.Merge(ascii_pb_str, pb)

  ValidatePb(pb);
  platform_type = PlatformTypes()[opts.type]
  filtered_pb = FilterPbForPlatform(pb, platform_type);

  # Serialize it
  binary_pb_str = filtered_pb.SerializeToString()

  # Write it to disk
  open(opts.outfile, 'wb').write(binary_pb_str)


def main():
  parser = optparse.OptionParser()
  # TODO(nparker): Remove this once the bug is fixed.
  parser.add_option('-w', '--wrap', action="store_true", default=False,
                     help='Wrap this script in another python '
                     'execution to disable site-packages.  This is a '
                     'fix for http://crbug.com/605592')
  parser.add_option('-t', '--type',
                    help='The platform type. One of android, chromeos, ' +
                    'linux, mac, win')
  parser.add_option('-i', '--infile',
                    help='The ASCII DownloadFileType-proto file to read.')
  parser.add_option('-o', '--outfile',
                    help='The binary file to write to.')
  parser.add_option('-p', '--path', action="append",
                    help='Repeat this as needed.  Directory(s) containing ' +
                    'the download_file_types_pb2.py and ' +
                    'google.protobuf.text_format modules')
  (opts, args) = parser.parse_args()
  if opts.infile is None or opts.outfile is None:
    parser.print_help()
    return 1

  if opts.wrap:
    # Run this script again with different args to the interpreter.
    command = [sys.executable, '-S', '-s', sys.argv[0]]
    command += ['-t', opts.type]
    command += ['-i', opts.infile]
    command += ['-o', opts.outfile]
    for path in opts.path:
      command += ['-p', path]
    sys.exit(subprocess.call(command))

  ImportProtoModules(opts.path)

  if (opts.type not in PlatformTypes()):
    print "ERROR: Unknown platform type '%s'" % opts.type
    parser.print_help()
    return 1

  try:
    GeneratBinaryProtos(opts)
  except Exception as e:
    print "ERROR: Failed to render binary version of %s:\n  %s\n" % (
        opts.infile, str(e))
    return 1


if __name__ == '__main__':
  sys.exit(main())
