import json, re

with open('.scratch/issue_snapshot_t0.json') as f:
    issues = json.load(f)

odd_issues = [i for i in issues if i['number'] % 2 == 1]
odd_issues = sorted(odd_issues, key=lambda x: x['number'])

print(f"Total Odd Issues: {len(odd_issues)}")

table = []
for i in odd_issues:
    num = i['number']
    title = i['title']
    body = i.get('body', '')
    
    # parse severity from title or body
    sev_match = re.search(r'\[(P[0-4])\]', title)
    sev = sev_match.group(1) if sev_match else "P2"
    
    # parse area/category
    type_match = re.search(r'## Type\s*\n+([^\n]+)', body)
    itype = type_match.group(1).strip() if type_match else "unknown"
    
    area_match = re.search(r'## Area\s*\n+([^\n]+)', body)
    area = area_match.group(1).strip() if area_match else "unknown"
    
    # parse affected code
    aff_match = re.search(r'## Affected code\s*\n+([^\n#]+(?:\n\-[^\n#]+)*)', body)
    aff_files = aff_match.group(1).strip() if aff_match else ""
    
    # parse findings
    findings = re.findall(r'([A-Z0-9_\-]+-\d+)', body)
    unique_findings = sorted(list(set(findings)))
    
    table.append({
        "num": num,
        "title": title,
        "sev": sev,
        "type": itype,
        "area": area,
        "files": aff_files,
        "findings": unique_findings
    })

print("| # | Sev | Type | Title | Findings |")
print("|---|---|---|---|---|")
for row in table:
    print(f"| #{row['num']} | {row['sev']} | {row['type']} | {row['title'][:70]} | {', '.join(row['findings'][:4])} |")
