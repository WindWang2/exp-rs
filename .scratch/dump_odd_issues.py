import json

with open('.scratch/issue_snapshot_t0.json') as f:
    issues = json.load(f)

odd_issues = [i for i in issues if i['number'] % 2 == 1]
odd_issues = sorted(odd_issues, key=lambda x: x['number'])

with open('.scratch/odd_issues_full.md', 'w') as out:
    out.write(f"# AGY-1 Snapshot T0 Odd Issues ({len(odd_issues)} issues)\n\n")
    for i in odd_issues:
        out.write(f"## Issue #{i['number']}: {i['title']}\n")
        out.write(f"- URL: {i['url']}\n")
        out.write(f"- Labels: {', '.join([l['name'] for l in i.get('labels', [])])}\n\n")
        out.write("### Body:\n```\n")
        out.write(i.get('body', ''))
        out.write("\n```\n\n---\n\n")

print(f"Wrote {len(odd_issues)} issues to .scratch/odd_issues_full.md")
