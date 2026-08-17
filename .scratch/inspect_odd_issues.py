import json

with open(".scratch/issue_snapshot_t0.json") as f:
    issues = json.load(f)

odd_issues = [i for i in issues if i["number"] % 2 == 1]
odd_issues = sorted(odd_issues, key=lambda x: x["number"])

print(f"Total Odd Issues: {len(odd_issues)}")
for i in odd_issues:
    print(f"\n==========================================")
    print(f"ISSUE #{i['number']}: {i['title']}")
    print(f"URL: {i['url']}")
    print(f"Labels: {[l['name'] for l in i.get('labels', [])]}")
    print(f"---------------- BODY ----------------")
    print(i['body'])
