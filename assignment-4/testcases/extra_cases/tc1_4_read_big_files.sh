marks=0
for file in "big_file" "huge_file"; do
	cp -f "$file" "../mnt/"
	ret=$?
	if [ $ret -ne 0 ]; then
		echo "$0: copy $file failed, returned $ret"
		exit $marks
	fi

	diff "$file" "../mnt/$file" > /dev/null
	ret=$?
	if [ $ret -ne 0 ]; then
		echo "$0: match failed for $file"
		exit $marks
	fi
	marks=$(( marks + 1 ))
done
exit $marks
