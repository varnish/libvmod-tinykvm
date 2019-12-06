#!/bin/bash
logger "Creating a fuzzer complaint package"
EMAIL=alf@varnish-software.com
FILEBIN="https://filebin.varnish-software.com"
# Generate BIN postfix
ID="$(hostname)-$(date +'%Y-%m-%d')"
FILENAME="autofuzz.${ID}.tar.gz"

cd /home/alf/git/varnish_autoperf/autofuzz

# Gather logs, binaries and core dump
fuzzer_gather()
{
	mkdir -p files
	rm files/* || true
	mv fuzz-*.log files/ || true
	mv crash-*    files/ || true
	sudo journalctl -u fuzzer > files/fuzzer.log
	sudo journalctl -xb -1 > files/journal.txt
	#coredumpctl dump --output files/core.dump > files/coredump.txt || true
	mv /tmp/crash/* files/ || true
	cp build/varnishd files/
}

# Upload fuzzer logs to filebin using curl
filebin_upload()
{
	BIN=$(head /dev/urandom | tr -dc a-z0-9 | head -c16)
	CURLSTATUS=$(curl --data-binary "@$FILENAME" $FILEBIN \
		-H "bin: ${BIN}" -H "filename: $FILENAME" \
		--progress-bar --silent --output /dev/null \
		--connect-timeout 60 --max-time 1800 \
		--write-out '%{http_code}')
	if [ ! "$CURLSTATUS" -eq "201" ]
	then
		echo "Failed to upload $FILENAME to $FILEBIN, http status code: $CURLSTATUS"
		return 1
	fi
	echo "==============================================================================="
	echo "autofuzz archive: $FILENAME"
	echo "Uploaded to: $FILEBIN/$BIN"
	echo "==============================================================================="
}

# 1. gather logs, binaries, core dump
fuzzer_gather
# 2. create and upload archive
tar -czvf $FILENAME files/
filebin_upload
# 3. send mail to tech@varnish-software.com
logger "Sending mail to $EMAIL"
echo "The fuzzer has stopped. See the filebin logs here: $FILEBIN/$BIN" > /tmp/mailbody.txt
echo "You will probably need to load the coredump from an equivalent system
" >> /tmp/mailbody.txt
lsb_release -a >> /tmp/mailbody.txt
uname -a >> /tmp/mailbody.txt
cat /tmp/mailbody.txt | sudo mail -s "Fuzzer stopped" $EMAIL
