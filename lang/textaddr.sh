#!/bin/sh
#
# Version 1.0.1 Build 7
#
# textaddr.sh - multi-language support script
#  Compile progmem1.var and lang_en.txt files to textaddr.txt file (mapping of progmem addreses to text idenifiers)
#
# Input files:
#  progmem1.var
#  lang_en.txt
#
# Output files:
#  textaddr.txt
#
#
# Dscription of process:
#  check if input files exists
#  create sorted list of strings from progmem1.var and lang_en.txt
#  lines from progmem1.var will contain addres (8 chars) and english text
#  lines from lang_en.txt will contain linenumber and english text
#  after sort this will generate pairs of lines (line from progmem1 first)
#  result of sort is compiled with simple script and stored into file textaddr.txt
#
#############################################################################
# Change log:
# 30 May  2018, XPila,     Initial
#  9 June 2020, 3d-gussner, Added version and Change log
#  9 June 2020, 3d-gussner, colored output
#  2 Apr. 2021, 3d-gussner, Use `git rev-list --count HEAD textaddr.sh`
#                           to get Build Nr
#############################################################################

echo "$(tput setaf 2)textaddr.sh started$(tput sgr0)" >&2

if [ ! -e progmem1.var ]; then echo "$(tput setaf 1)textaddr.sh - file progmem1.var not found!$(tput sgr0)" >&2; exit 1; fi 
if [ ! -e lang_en.txt ]; then echo "$(tput setaf 1)textaddr.sh - file lang_en.txt not found!$(tput sgr0)" >&2; exit 1; fi 
addr=''
text=''
(cat progmem1.var | sed -E "s/^([^ ]*) ([^ ]*) (.*)/\1 \"\3\"/";\
 cat lang_en.txt | sed "/^$/d;/^#/d" | sed = | sed '{N;s/\n/ /}') |\
 sort -k2 |\
 sed "s/\\\/\\\\\\\/g" | while read num txt; do
 if [ ${#num} -eq 8 ]; then
  if [ -z "$addr" ]; then
   addr=$num
  else
   if [ "$text" = "$txt" ]; then
    addr="$addr $num"
   else
    echo "ADDR NF $addr $text"
    addr=$num
   fi
  fi
  text=$txt   
 else
  if [ -z "$addr" ]; then
   if ! [ -z "$num" ]; then echo "TEXT NF $num $txt"; fi
  else
   if [ "$text" = "$txt" ]; then
    if [ ${#addr} -eq 8 ]; then
     echo "ADDR OK $addr $num"
    else
     echo "$addr" | sed "s/ /\n/g" | while read ad; do
      echo "ADDR OK $ad $num"
     done
    fi
    addr=''
    text=''
   else
    if ! [ -z "$num" ]; then echo "TEXT NF $num $txt"; fi
   fi
  fi
 fi
done > textaddr.txt

echo "$(tput setaf 2)textaddr.sh finished$(tput sgr0)" >&2

exit 0
