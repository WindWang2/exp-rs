import re, json

with open('.scratch/issue_snapshot_t0.json') as f:
    issues = json.load(f)

odd_issues = [i for i in issues if i['number'] % 2 == 1]
odd_issues = sorted(odd_issues, key=lambda x: x['number'])

with open('.scratch/detailed_analysis.md', 'w') as out:
    for iss in odd_issues:
        num = iss['number']
        title = iss['title']
        body = iss.get('body', '')
        out.write(f"# #{num}: {title}\n\n")
        
        # extract sections
        for sec in ["Area", "Type", "Severity", "Affected code", "User impact", "Reproduction", "Expected behavior", "Actual behavior", "Root cause", "Suggested fix", "References / Dedupe"]:
            m = re.search(r'## ' + sec + r'\s*\n(.*?)(?=\n## |\Z)', body, re.DOTALL)
            if m:
                out.write(f"### {sec}\n{m.group(1).strip()}\n\n")
        out.write("\n---\n\n")

print(f"Wrote detailed analysis to .scratch/detailed_analysis.md")
