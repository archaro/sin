#!/bin/bash
# Iterate through the srcroot directory, constructing a sinistra script
# to recreate every code item from its source.
#
# Courtesy of ChatGPT.  I don't speak bash.

# Root directory to start searching from
if [ "$#" -eq 1 ]; then
  rootdir="$1" # Use the provided argument as rootdir
else
  rootdir="srcroot" # Default value for rootdir
fi

# Find all 'source.sin' files under 'srcroot', including subdirectories
find "$rootdir" -type f -name 'source.sin' -print0 | while IFS= read -r -d '' file; do
  # Extract the path excluding 'srcroot/' prefix
  relative_path="${file#$rootdir/}"

  # Remove '/source.sin' from the end of the path
  path_without_file="${relative_path%/source.sin}"

  # Convert slashes to dots, starting from the first character to avoid leading dot
  final_path=$(echo "$path_without_file" | sed 's/\//./g')

  echo -n "${final_path} = "
  cat "$file"
  echo # Add a newline for readability between files
done

