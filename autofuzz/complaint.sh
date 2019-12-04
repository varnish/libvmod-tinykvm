#!/bin/bash
logger "Creating a fuzzer complaint package"
EMAIL=alf@varnish-software.com

cd /home/alf/git/varnish_autoperf/autofuzz

# 1. create directory for error report
mkdir -p files
rm files/* || true
mv fuzz-*.log files/ || true
mv crash-*    files/ || true
sudo journalctl -xb -1 > files/journal.txt
coredumpctl dump --output files/core.dump > files/coredump.txt || true
cp build/varnishd files/
# 2. mail body
echo "The fuzzer has stopped" > /tmp/mailbody.txt
# 3. archive crashes and logs
tar -czvf logs.tar.gz files/
# 4. send mail to tech@varnish-software.com
logger "Sending mail to $EMAIL"
cat /tmp/mailbody.txt | sudo mail -s "Fuzzer stopped" -a logs.tar.gz $EMAIL
