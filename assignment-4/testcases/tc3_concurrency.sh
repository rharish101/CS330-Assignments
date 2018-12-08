random_test() {
	thread_num=$2
	num_ops=$3
	op=0
	file_num=$(( thread_num * num_ops ))
	while [[ $op -lt $num_ops ]]; do
		file="$1_file_$file_num"
		file1="tmp_files/$file"
		file2="../mnt/$file"
		if [ ! -e $file1 ]; then
			dd if=/dev/urandom of=$file1 bs=10K count=1 2> /dev/null
		fi
		cp -f $file1 $file2
		if [ $? -ne 0 ]; then
			>&2 echo "$1: copying failed"
			return 0
		fi
		passed=0
		diff $file1 $file2
		if [ $? -ne 0 ]; then
			>&2 echo "$1: No match between $file1 and $file2"
			return 0
		fi
		op_=$RANDOM
		op_=$(( op_ % 3 ))
		if [[ $op_ -eq 0 ]]; then
			# double creation test
			./create $file "dummy" 2>&1
			ret=$?
			if [ $ret -ne 10 ]; then
				>&2 echo "$0: double creation test failed, returned $ret"
				return 0
			fi
		elif [[ $op_ -eq 1 ]]; then
			# deletion test
			./delete $file
			ret=$?
			if [ $ret -ne 0 ]; then
				>&2 echo "$1: deletion failed, returned $ret"
				return 0
			fi

			(stat $file2 3>&2 2>&1 1>&3)
			ret=$?
			if [ $ret -ne 1 ]; then
				>&2 echo "$1: deletion actually failed; stat returned $ret"
				return 0
			fi
		elif [[ $op_ -eq 2 ]]; then
			# rename test
			new_name=$file"_new"
			./rename $file $new_name
			ret=$?
			if [ $ret -ne 0 ]; then
				>&2 echo "$1: rename failed, returned $ret"
				return 0
			fi

			(stat $file2 3>&2 2>&1 1>&3)
			ret=$?
			if [ $ret -ne 1 ]; then
				>&2 echo "$1: rename actually failed - old file ($file2) found; stat returned $ret"
				return 0
			fi

			stat "../mnt/"$new_name
			ret=$?
			if [ $ret -ne 0 ]; then
				>&2 echo "$1: rename actually failed - new file ($new_name) missing; stat returned $ret"
				return 0
			fi
			diff $file1 "../mnt/"$new_name
			if [ $? -ne 0 ]; then
				>&2 echo "$1: No match between $file1 and $new_name"
				return 0
			fi
		fi
		op=$(( op + 1 ))
		file_num=$(( file_num + 1 ))
	done
	return 1
}

FILENAME=$(basename -- "$0")
FILENAME="${FILENAME%%.*}"

./init_disk.sh 1024
passed=0
max_t=16
i=1
pids=()
while [[ $i -le $max_t ]]; do
	random_test $FILENAME $i 4 &
	#echo "pid = $!"
	pids+=($!)
	i=$(( i + 1 ))
done
for pid in ${pids[@]}; do
	#echo "waiting for $pid"
	wait $pid
	ret=$?
	passed=$(( passed + ret ))
done
echo "$0: passed = $passed"
./init_disk.sh -d
if [ $passed -eq $max_t ]; then
	exit 4		# 2 marks
fi

exit 0
