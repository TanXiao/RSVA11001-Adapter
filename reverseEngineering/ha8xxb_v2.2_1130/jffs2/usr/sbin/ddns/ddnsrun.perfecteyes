#!/bin/sh
##################################################
# ddns client
# Author: leitz@sin360.net 2007.04.25
##################################################
DIRINCLUDE="/usr/sbin/ddns"
DEFAULTDNSSERVER="www.perfecteyes.com"

. ${DIRINCLUDE}/ddnsrc

. ${CONFIGFILE}

#check config
if [ "${DOMAINNAME}" == "" ] ||  [ "${USERNAME}" == "" ] || [ "${USERPASSWORD}" == "" ]
then
	exit 1
fi
#set default DNSSERVER
if [ "${DNSSERVER}" == "" ]
then
	DNSSERVER="${DEFAULTDNSSERVER}"
fi

SUCCESSFLAG="good"
NOCHANGEFLAG="nochg"

RegisterDomain()
{
#	deletefile ${OUTPUTFILE}
	URL="http://${USERNAME}:${USERPASSWORD}@${DNSSERVER}/ddns/update?hostname=${DOMAINNAME}&myip=${IPAddress}"
	echo "DDNS URL:${URL}"
	${WGETCMD} -O ${OUTPUTFILE} ${URL}
	result="$?"
	if [ "${result}" == "0" ]
	then
		result=`grep ${SUCCESSFLAG} ${OUTPUTFILE}`
		if [ $? == 1 ] || [ "$result" == "" ] 
		then
			result=`grep ${NOCHANGEFLAG} ${OUTPUTFILE}`
			if [ $? == 1 ] || [ "$result" == "" ] 
			then
				echo "DDNS Register ${DOMAINNAME} ${IPAddress} Failed!"
				exit 1
			fi
		fi
		echo "DDNS Register ${DOMAINNAME} ${IPAddress} OK!"
		exit 0
	else
		echo "DDNS Register ${DOMAINNAME} ${IPAddress} Error!"
		exit 2
	fi
}

IPAddress=""
[ "${ISPROXY}" == "y" ]  || [ "${ISPROXY}" == "yes" ]  || [ "${ISPROXY}"  == "1" ] || GetIPAddress

RegisterDomain

exit 0
