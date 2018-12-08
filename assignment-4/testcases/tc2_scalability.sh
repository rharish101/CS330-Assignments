random_test() {
	file="$1_file_$2"
	file1="tmp_files/$file"
	file2="../mnt/$file"
	if [ ! -e $file1 ]; then
		dd if=/dev/urandom of=$file1 bs=$3 count=$4 2> /dev/null
	fi
	cp -f $file1 $file2
	if [ $? -ne 0 ]; then
		return 0
	fi
	>&2 diff $file1 $file2
	if [ $? -eq 0 ]; then
		return 1
	fi
	return 0
}

FILENAME=$(basename -- "$0")
FILENAME="${FILENAME%%.*}"

#max size of files test
./init_disk.sh 5000	# x1MB ~ 5GB
passed=0
max=100
i=1
while [[ $i -le $max ]]; do
	random_test $FILENAME $i 1048576 16	#16MB file * 100
	ret=$?
	echo "$i - $ret"
	if [ $ret -eq 0 ]; then
		break
	fi
	passed=$(( passed + ret ))
	i=$(( i + 1 ))
done
echo "$0: Big files: passed = $passed"
if [ $passed -eq $max ]; then
	marks=$(( marks + 3 ))
fi


# max number of files test
./init_disk.sh 1024	# x1MB = 1GB
passed=0
#max=1000000
max=10000
i=1
j=1000001
while [[ $i -le $max ]]; do
	random_test $FILENAME $j 8000 1		#~8KB file * 10^6 = 8GB
	ret=$?
	if [ $ret -eq 0 ]; then
		break
	fi
	passed=$(( passed + ret ))
	i=$(( i + 1 ))
	j=$(( j + 1 ))
done
echo "$0: Large number of files: passed = $passed"
if [ $passed -eq $max ]; then
	marks=$(( marks + 3 ))
fi

exit $marks
