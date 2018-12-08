FILENAME=$(basename -- "$0")
FILENAME="${FILENAME%%.*}_file1"
FILECONTENT="file1 content to delete"

./delete $FILENAME		# just try delete before creating

./create $FILENAME "$FILECONTENT"
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: creation failed, returned $ret"
	exit 0
fi

./delete $FILENAME
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: deletion failed, returned $ret"
	exit 0
fi

stat ../mnt/$FILENAME
ret=$?
if [ $ret -ne 1 ]; then
	echo "$0: deletion actually failed; stat returned $ret"
	exit 0
fi
exit 1
