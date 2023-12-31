#!/bin/bash

# Write a shell script finder-app/writer.sh as described below
#
#    * Accepts the following arguments: the first argument is a full path to a file (including filename) on the 
#       filesystem, referred to below as writefile; the second argument is a text string which will be written within 
#       this file, referred to below as writestr
#    * Exits with value 1 error and print statements if any of the arguments above were not specified
#    * Creates a new file with name and path writefile with content writestr, overwriting any existing file and creating 
#       the path if it doesn’t exist. Exits with value 1 and error print statement if the file could not be created.
#
# Example:
#       writer.sh /tmp/aesd/assignment1/sample.txt ios
# Creates file:
#
#    /tmp/aesd/assignment1/sample.txt
#
#            With content:
#
#            ios

if [ $# -ne 2 ]
then
    echo "Usage: writer.sh [filepath] [textstring]"
    echo "Example invocation: writer.sh /tmp/aesd/assignment1/sample.txt ios"
    exit 1
fi

filepath=$1
textstring=$2

echo ${filepath}
dirpath=$(dirname ${filepath})

if [ ! -d ${dirpath} ]
then
    if ! mkdir -p ${dirpath}
    then
        echo "Cannot create directory for file!"
        exit 1
    fi
fi


if ! touch ${filepath}
then
    echo "Cannot create or write to file!"
    exit 1
fi

echo "${textstring}" > ${filepath}
exit 0
