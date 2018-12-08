FILENAME=$(basename -- "$0")
FILENAME="${FILENAME%%.*}_file1"
FILENAME2="new_name"
FILECONTENT="file1 content to rename"

./delete $FILENAME		# just try delete before creating
./delete $FILENAME2		# just try delete before creating

./create $FILENAME "$FILECONTENT"
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: creation failed, returned $ret"
	exit 0
fi

./rename $FILENAME $FILENAME2
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: rename failed, returned $ret"
	exit 0
fi

stat ../mnt/$FILENAME
ret=$?
if [ $ret -ne 1 ]; then
	echo "$0: rename actually failed - old file found; stat returned $ret"
	exit 0
fi

stat ../mnt/$FILENAME2
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: rename actually failed - new file missing; stat returned $ret"
	exit 0
fi

read_content=$(./read $FILENAME2 ${#FILECONTENT})
ret=$?
if [ $ret -ne 0 ]; then
	echo "$0: reading failed, returned $ret"
	exit 0
fi

if [ "$FILECONTENT" == "$read_content" ]; then
	exit 1
else
	echo "$0: match failed"
	exit 0
fi
exit 1
