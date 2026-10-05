L1=$1
H1=$2
L2=$3
H2=$4

merge() {
	EPROM1=$1
	EPROM2=$2
	TEMPROM=$3

	BYTES=$(ls -l $EPROM1 | awk '{print $5}')
	SKIP=0
	SEEK=0

	while [ ${SKIP} -lt ${BYTES} ] ; do
		dd if=$EPROM1 of=$TEMPROM seek=$SEEK skip=$SKIP count=1 bs=1 2>/dev/null
		SEEK=$(( $SEEK + 1))
		dd if=$EPROM2 of=$TEMPROM seek=$SEEK skip=$SKIP count=1 bs=1 2>/dev/null
		SEEK=$(( $SEEK + 1))
		SKIP=$(( $SKIP + 1))
	done
}

merge $H1 $L1 temprom.1.$$ &
merge $H2 $L2 temprom.2.$$ &
wait
cat temprom.2.$$ >> temprom.1.$$
mv temprom.1.$$ rom.$$
rm -f temprom.2.$$
