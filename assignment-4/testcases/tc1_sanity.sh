random_test() {
	file="$1_file_$2"
	file1="tmp_files/$file"
	file2="../mnt/$file"
	flag=0
	if [ ! -e $file1 ]; then
		dd if=/dev/urandom of=$file1 bs=$3 count=$4 2> /dev/null
	fi
	cp -f $file1 $file2		#put_key()
	if [ $? -ne 0 ]; then
		>&2 echo "$1: cp failed"
		return $flag
	fi
	fsize1=`stat --format=%s $file1`
	fsize2=`stat --format=%s $file2` #lookup_key
	if [ $? -ne 0 ]; then
		>&2 echo "$1: Created file not found"
		return 0
	fi
	flag=1			#0001
	if [ $fsize1 -ne $fsize2 ]; then
		>&2 echo "$1: wrong size"
	else
		diff $file1 $file2		#get_key()
		if [ $? -eq 0 ]; then
			flag=3		#0011
		else
			>&2 echo "$1: wrong content"
		fi
	fi
	op_=$RANDOM
	op_=$(( op_ % 2 ))
	if [[ $op_ -eq 0 ]]; then
		flag=11		#X011
		# deletion test
		./delete $file
		ret=$?
		if [ $ret -ne 0 ]; then
			>&2 echo "$1: deletion failed, returned $ret"
			return $flag
		fi

		(stat $file2 3>&2 2>&1 1>&3)
		ret=$?
		if [ $ret -ne 1 ]; then
			>&2 echo "$1: deletion actually failed; stat returned $ret"
			return $flag
		fi

		# negative test case for delete
		./delete "random_file"
		ret=$?
		if [ $ret -eq 0 ]; then
			>&2 echo "$1: negative deletion failed, returned $ret"
			return $flag
		fi
	else
		flag=7		#0X11
		# rename test
		new_name=$file"_new"
		./rename $file $new_name
		ret=$?
		if [ $ret -ne 0 ]; then
			>&2 echo "$1: rename failed, returned $ret"
			return $flag
		fi

		(stat $file2 3>&2 2>&1 1>&3)
		ret=$?
		if [ $ret -ne 1 ]; then
			>&2 echo "$1: rename actually failed - old file ($file2) found; stat returned $ret"
			return $flag
		fi

		stat "../mnt/"$new_name
		ret=$?
		if [ $ret -ne 0 ]; then
			>&2 echo "$0: rename actually failed - new file ($new_name) missing; stat returned $ret"
			return $flag
		fi
		diff $file1 "../mnt/"$new_name
		if [ $? -ne 0 ]; then
			>&2 echo "$1: No match between $file1 and $new_name"
			return $flag
		fi

		# negative test cases for rename
		./rename "random_file_mv" "mv_random_file"
		ret=$?
		if [ $ret -eq 0 ]; then
			>&2 echo "$1: negative rename test1 failed, returned $ret"
			return $flag
		fi

		cp -f $file1 $file2
		./rename $file $new_name
		ret=$?
		if [ $ret -eq 0 ]; then
			>&2 echo "$1: negative rename test2 failed, returned $ret"
			return $flag
		fi
	fi
	flag=15			#1111
	return $flag
}

FILENAME=$(basename -- "$0")
FILENAME="${FILENAME%%.*}"

./init_disk.sh 1024

main_flag=15			#1111
for i in {1..100}; do
	size=$RANDOM
	random_test $FILENAME $i $size 1
	ret=$?
	main_flag=$(( main_flag & ret ))
	if [ $main_flag -eq 0 ]; then
		exit 0
	fi
done
marks=0
put_key_passed=$(( main_flag & 1 ))
main_flag=$(( main_flag >> 1 ))
get_key_passed=$(( main_flag & 1 ))
main_flag=$(( main_flag >> 1 ))
delete_key_passed=$(( main_flag & 1 ))
main_flag=$(( main_flag >> 1 ))
rename_key_passed=$(( main_flag & 1 ))

if [ $put_key_passed -eq 1 ]; then
	marks=$(( marks + 2 ))
fi
if [ $get_key_passed -eq 1 ]; then
	marks=$(( marks + 2 ))
fi
if [ $delete_key_passed -eq 1 ]; then
	marks=$(( marks + 1 ))
fi
if [ $rename_key_passed -eq 1 ]; then
	marks=$(( marks + 1 ))
fi

./init_disk.sh -d

exit $marks	# * 0.5 to scale
