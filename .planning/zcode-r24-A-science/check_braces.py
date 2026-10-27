import glob, re, sys
str_re = re.compile(r'"(?:\\.|[^"\\])*"')
bad = 0
for f in sorted(glob.glob('tests/test_r24_a_*.cpp')):
    s = str_re.sub('""', open(f, encoding='utf-8').read())
    s = re.sub(r'//[^\n]*', '', s)
    b = s.count('{') - s.count('}')
    p = s.count('(') - s.count(')')
    flag = '' if (b == 0 and p == 0) else '  <-- UNBALANCED'
    if flag:
        bad += 1
    print(f, 'braces', b, 'parens', p, flag)
sys.exit(1 if bad else 0)
