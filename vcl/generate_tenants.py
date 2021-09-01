import os
import sys

output_c = sys.argv[1]

file = open(output_c, "w")
file.write('{\n')
for x in range(0, 400):
    file.write('    "tenant' + str(x) + '.com": {\n')
    file.write('        "filename": "/tmp/xpizza",\n')
    file.write('        "key": "12daf155b8508edc4a4b8002264d7494",\n')
    file.write('        "group": "test"\n')
    file.write('    },\n')
file.write('    "dummy": {}\n')
file.write('}\n')
file.write('\n')

print(output_c)
