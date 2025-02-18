
criteria_file = "criteria.txt"
my_answer = "my.txt"

criteria = {}
my = {}
grade = {
    "total": 0,
    "name_correct": 0,
    "sha1_correct": 0,
}

with open(criteria_file, 'r') as f:
    lines = f.readlines()
    lines = [line for line in lines if line.endswith(".bmp\n")]
    grade["total"] = len(lines)
    for line in lines:
        sha1, filename = tuple(line.strip().split())
        criteria[filename] = sha1

with open(my_answer, 'r') as f:
    lines = f.readlines()
    lines = [line for line in lines if line.endswith(".bmp\n")]
    for line in lines:
        sha1, filename = tuple(line.strip().split())
        my[filename] = sha1
        if filename in criteria:
            if criteria[filename] == sha1:
                grade["sha1_correct"] += 1
            grade["name_correct"] += 1

for filename in criteria:
    if filename not in my:
        print("File %s not found in my answer" % filename)

percent_name, percent_sha1 = grade["name_correct"] / grade["total"], grade["sha1_correct"] / grade["total"]
print(f"NAME correct: {grade['name_correct']}/{grade['total']} = {100 * percent_name:.2f}%")
print(f"SHA1 correct: {grade['sha1_correct']}/{grade['total']} = {100 * percent_sha1:.2f}%")
