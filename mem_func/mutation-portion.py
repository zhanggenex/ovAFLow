import os
import scipy.stats as stats
import numpy as np

g = os.walk("./")

afl_new = [ "./AFL-new", "./AFL-loop"]

afl_old = ["./AFL", "./aflfast", "./afl-rb"]

cnt_dic = {}

portion_dic = {}

for path, dir_list, file_list in g:
	for dir_name in dir_list:
		main_dir = os.path.join(path, dir_name)
		if main_dir == "./AFL-loop" or main_dir == "./AFL-new":
			print "\n\n"
			print main_dir
			afl = os.walk(os.path.join(path, dir_name))
			for afl_path, afl_dir, afl_file in afl:
				for tar_name in afl_dir:
					if "readelf" in tar_name:
						print(os.path.join(afl_path, tar_name))
						tar_dir = os.path.join(afl_path, tar_name)

						crash_dir = tar_dir + "/crashes"
						
						crash_cnt = -1
						mutate_cnt = 0
						for i in os.listdir(crash_dir):
							if "ext_AO" in i or "int32" in i or "arith32" in i or "flip32" in i:
								mutate_cnt += 1
							crash_cnt += 1
						if crash_cnt == -1:
							crash_cnt = 0

						print "*************************************************"
						if crash_cnt == 0:
							mutate_portion = 0.0
						else:
							mutate_portion = float(mutate_cnt) / float(crash_cnt)
						print mutate_cnt, "/", crash_cnt, mutate_portion
						print "*************************************************"

						if main_dir not in cnt_dic:
							cnt_dic.setdefault(main_dir, [])
							cnt_dic[main_dir].append(mutate_cnt)
						else:
							cnt_dic[main_dir].append(mutate_cnt)

						if main_dir not in portion_dic:
							portion_dic.setdefault(main_dir, [])
							portion_dic[main_dir].append(mutate_portion)
						else:
							portion_dic[main_dir].append(mutate_portion)

						print cnt_dic, portion_dic

for i in afl_new:
	print "##############", i ,"###############"
	print "Result of mutate cnt:"
	print "Average: ", np.mean(cnt_dic[i])
	print "Max: ", np.max(cnt_dic[i])
	print "Min: ", np.min(cnt_dic[i])
	print "\n"
	print "Result of mutate portion:"
	print "Average: ", np.mean(portion_dic[i])
	print "Max: ", np.max(portion_dic[i])
	print "Min: ", np.min(portion_dic[i])
	print "\n"

