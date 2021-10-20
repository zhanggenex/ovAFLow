asan = open("total-asan")

real_crash = []

for line in asan:
	if "SUMMARY: AddressSanitizer:" in line:
		if line not in real_crash:
			real_crash.append(line)

print real_crash

asan.close()
