#!/bin/bash -
# arg1 is the build directory
# arg2 the input binary file
# arg3 output binary anme
projectdir="${1}"
infile="${2}"
outfile="${3}"
binary_type="${4}"
entry_point="${5}"

echo ${projectdir}
SOURCE="Projects"
slash=""
basedir="../../../ImageHeader"


case "$(uname -s)" in
   Linux)
      #line for python
      echo Postbuild with python script
      imgtool="${basedir}/Python27/Stm32ImageAddHeader.py"
      cmd="python"
      ;;
   *)
      #line for window executeable
      echo Postbuild with windows executable
      imgtool="Stm32ImageAddHeader"
      cmd=""
      ;;
esac

message="${infile}'s signature done. Output file: ${outfile}"
command="${cmd} ${imgtool} ${infile} ${outfile} ${binary_type} ${entry_point}"
ret 1
${command} # > "${projectdir}/output_log.txt"
ret=$?

if [ ${ret} == 0 ]; then
echo ${message}
else
echo "postbuild.sh failed" ${ret} ${command}
fi
