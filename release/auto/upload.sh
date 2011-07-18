#!/bin/sh

TYPE=$1
REL=$2
[[ "$TYPE" == "" ]] && exit 1
[[ "$REL" == "" ]] && exit 1

host=kang.project-voodoo.org
dest=/home/kang/web/f/CM7/$TYPE/

#SF.NET - deprecated
#path=/home/frs/project/c/cm/cmkernel/CM7/
#host=kangbooboo@frs.sf.net

#f=$(mktemp)

#cat > $f << END
#cd ${path}
#cd ${TYPE}
#put release/${TYPE}/${REL}
#END

#ret=0
#sftp -b $f $host || ret=1
#rm $f
#exit $ret

#Voodoo
scp release/$TYPE/$REL.sha256sum release/$TYPE/$REL $host:$dest
