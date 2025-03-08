with open('test64k.txt', 'w') as f:
    f.write('*' * 65540)
    f.write('hello\n')
    f.write('*' * 65540)
    f.write('world\n')
