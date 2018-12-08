FILENAME=$(basename -- "$0")
FILENAME="${FILENAME%%.*}_file1"
FILECONTENT="file1 content to create"

./create $FILENAME "$FILECONTENT"
ret=$?
if [ $ret -ne 0 ]; then
	>&2 echo "$0: creation failed, returned $ret"
	exit 0
fi

read_content=$(./read $FILENAME ${#FILECONTENT})
ret=$?
if [ $ret -ne 0 ]; then
	>&2 echo "$0: reading failed, returned $ret"
	exit 0
fi

if [ "$FILECONTENT" == "$read_content" ]; then
	exit 1
else
	>&2 echo "$0: match failed"
	exit 0
fi
