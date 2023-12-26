import os
import re

def rename_includes(header_file, kernel_directory):
    with open(header_file, 'r') as file:
        content = file.read()

    # Define a regular expression pattern to match #include <filename>
    pattern = r'#include\s*<([^>]*)>'

    def replace(match):
        filename = match.group(1)
        relative_path = os.path.relpath(os.path.join(kernel_directory, filename), os.path.dirname(header_file))
        return f'#include "{relative_path}"'

    # Use re.sub to replace the matched patterns
    new_content = re.sub(pattern, replace, content)

    with open(header_file, 'w') as file:
        file.write(new_content)

def process_directory(directory, kernel_directory):
    for root, dirs, files in os.walk(directory):
        for file in files:
            file_path = os.path.join(root, file)

            if file.endswith('.h'):
                print("processing file > {}".format(file))
                rename_includes(file_path, kernel_directory)

# Example usage:
kernel_directory = 'kernel_folder'
project_directory = 'testpilot'

process_directory(project_directory, kernel_directory)
