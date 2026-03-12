# direnv_src.sh - Captures env-var diff before/after sourcing a script and writes deltas to .envrc.
#
# Defines a shell function `direnvsrc` that snapshots environment variables,
# sources a given script, then diffs the environment to find new or changed
# variables. The deltas are appended as `export VAR=value` lines to an .envrc
# file (or a custom output path).
#
# Usage:
#     source direnv_src.sh
#     direnvsrc <script_to_source> [output_file]
#
# Dependencies:
#     realpath (coreutils)

direnvsrc() {
    # Ensure a script was provided
    if [ -z "$1" ]; then
        echo "Please provide a script to source."
        return 1
    fi

    # Default output to .envrc in the current directory if not provided
    output=${2:-"./.envrc"}

    # Store current environment
    oldenv="$(mktemp)"
    env > "$oldenv"

    echo -e "\nOld environment variables have been written to $oldenv:"
    cat "$oldenv"

    # Check if 'realpath' exists, if not, `brew install coreutils`
    if ! command -v realpath &> /dev/null; then
        echo "realpath could not be found, please install it with 'brew install coreutils'"
        return 1
    fi

    # Get the full path to the script
    script="$(realpath $1)"

    # Source the provided script
    . "$script"

    # Store new environment
    newenv="$(mktemp)"
    env > "$newenv"

    echo -e "\nNew environment variables have been written to $newenv:"
    cat "$newenv"

    # Check if we need to add a new line to the output file first
    if [ ! -f "$output" ]; then
        touch "$output"
    else
        # Remove all empty lines from the end of the file
        sed -i '' -e :a -e '/^\n*$/{$d;N;};/\n$/ba' "$output"
        echo "" >> "$output"
    fi

    # Find differences and format them for direnv (new lines and modified lines)
    grep -Fxvf "$oldenv" "$newenv" | cut -d= -f1 | while read -r line; do
        echo "Running: $line"
        # Check if this line starts with "PATH"
        if [ "$line" = "PATH" ]; then
            # Only append new paths
            old_path=$(grep "^PATH=" "$oldenv" | cut -d= -f2-)
            new_path=$(grep "^PATH=" "$newenv" | cut -d= -f2-)
            # Get new paths that aren't in the old path
            echo "$old_path" | tr : '\n' | sort > temp_old.txt
            echo "$new_path" | tr : '\n' | sort > temp_new.txt
            new_paths=$(comm -13 temp_old.txt temp_new.txt | tr '\n' : | sed 's/:$//')
            # Don't forget to clean up temp files
            rm temp_old.txt temp_new.txt
            if [ -n "$new_paths" ]; then
                # Use direnv's PATH_add command to add the new paths
                echo "PATH_add $new_paths" >> "$output"
            else
                echo "No new paths to add"
            fi
        else
            new_entry="export $line=\"$(grep "^$line=" "$newenv" | cut -d= -f2-)\""
            # Only append if this line doesn't already exist in the file
            if ! grep -Fxq "$new_entry" "$output"; then
                echo "$new_entry" >> "$output"
            fi
        fi
    done

    # Delete duplicated lines (ignoring whitespace)
    awk '!x[$0]++' "$output" > "$output.tmp" && mv "$output.tmp" "$output"

    echo -e "\n\nNew environment variables have been written to $output:"
    cat "$output"

    # Clean up
    rm "$oldenv"
    rm "$newenv"

    direnv allow
}
