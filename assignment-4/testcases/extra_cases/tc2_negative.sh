FILENAME=$(basename -- "$0")
FILENAME="${FILENAME%%.*}_file1"
FILECONTENT="file1 content to create"

tc2_create_test() {
	./create $FILENAME "$FILECONTENT"
	ret=$?
	if [ $ret -ne 0 ]; then
		echo "$0: creation failed, returned $ret"
		return 0
	fi

	./create $FILENAME "$FILECONTENT"
	ret=$?
	if [ $ret -ne 10 ]; then
		echo "$0: double creation test failed, returned $ret"
		return 0
	fi
	return 1
}

tc2_delete_test() {
	./delete $FILENAME
	ret=$?
	if [ $ret -ne 0 ]; then
		echo "$0: deletion failed, returned $ret"
		return 0
	fi

	./delete $FILENAME
	ret=$?
	if [ $ret -ne 10 ]; then
		echo "$0: double deletion test failed, returned $ret"
		return 0
	fi
	return 1
}

marks=0
tc2_create_test
marks=$(( marks + $? ))
tc2_delete_test
marks=$(( marks + $? ))

exit $marks
