import serial
import sys
import threading
import binascii
from time import sleep

# Original Source Website – https://github.com/eewilliac/PyRCX

#basic method for converting an opcode (a hex string) to a byte (expressed as decimal integer 0<x<=255)
def hex_to_int(opcode):
	hex_string=binascii.a2b_hex(opcode)
	return ord(hex_string) #ord returns the hex_string as a decimal integer.

def complement(opcode):
	#opcode is a pseudo hex string. so instead 0xef, I have ef
	print("opcode b4 complement:",opcode)
	hex_string=binascii.a2b_hex(opcode)
	new_opcode=~ord(hex_string) & 0xff # ord returns my hex string as a decimal integer
	return new_opcode

def int_to_hex(int_value):
	return hex(int_value)

def int_complement(int_value):
	hex_val=int_to_hex(int_value)
	byte_val=bytes.fromhex(hex_val)
	retValue=~(ord(byte_val)) & 0xff
	return retValue

def convert_opcode_to_int(raw_opcode):
	opcodelist=raw_opcode.split(' ')
	int_list=[]
	for opcode in opcodelist:
		int_value=hex_to_int(opcode)
		int_list.append(int_value)
	return int_list

def build_rcx_package(opcodelist):
	checksum=0
	opcodelist=opcodelist.split(' ')
	bytelist=bytearray()
	bytelist.append(hex_to_int('55'))
	bytelist.append(hex_to_int('ff'))
	bytelist.append(hex_to_int('00'))
	for opcode in opcodelist:
		int_value=hex_to_int(opcode)
		complement_value=complement(opcode)
		bytelist.append(int_value)
		bytelist.append(complement_value)
		checksum+=int_value
	
	checksum_as_hex=int_to_hex(checksum & 0xff) #checksum will be a hex integer
	print("checksum as hex:",checksum_as_hex)
	checksum_as_string=checksum_as_hex[2:] #grab last 2 chars because string is in form 0xFE

	bytelist.append(checksum & 0xff)
	print("checksum as string:",checksum_as_string)
	bytelist.append(complement(checksum_as_string))

	return bytelist
	

	# checksum_str=str(checksum)
	# complement_value=complement(checksum_str) & 0xff
	# bytelist.append(checksum)
	# bytelist.append(complement_value)
	# print(*bytelist,sep=",")

def build_rcx_package2(opcodelist):
	checksum=0
	list_of_ints=convert_opcode_to_int(opcodelist)
	bytelist=bytearray()
	bytelist.append(hex_to_int('55'))
	bytelist.append(hex_to_int('ff'))
	bytelist.append(hex_to_int('00'))
	for int_value in list_of_ints:
		complement_value=complement(int_value)
		bytelist.append(int_value)
		bytelist.append(complement_value)
		checksum+=int_value


	# complement_checksum=int_complement(checksum)    	
	# bytelist.append(checksum)
	# bytelist.append(complement_checksum)
	return bytelist

def write_to_serial(raw_data):
	ser = serial.Serial(port='COM3',baudrate=2400,parity=serial.PARITY_ODD,stopbits=serial.STOPBITS_ONE,bytesize=serial.EIGHTBITS)               
	ser.write(raw_data)
	# print(raw_data)
	ser.close()


def send(opcodes):
	raw_data=build_rcx_package(opcodes)
	print(*raw_data,end=",")
	print()
	write_to_serial(raw_data)
	sleep(5)
	raw_data=build_rcx_package("10 10")
	write_to_serial(raw_data)
	sleep(1)

def test2(raw_opcode):
	opcodelist=raw_opcode.split(' ')
	for hex_value in opcodelist:
		int_value=hex_to_int(hex_value)
		print(int_value)

def test3(raw_opcode):
	x=convert_opcode_to_int(raw_opcode)		
	for num in x:
		hex_str=int_to_hex(num) #this is fine, but are we returning strings.
		int_val=hex_to_int(hex_str)
		print(hex_str)
		print(int_val)

def test4(raw_opcode):
	opcodelist=raw_opcode.split(' ')
	for opcode in opcodelist:
		print(hex_to_int(opcode))
		print(complement(opcode))

def test(raw_opcode):
	int_list=build_rcx_package(raw_opcode)
	for num in int_list:
		print(num)

if __name__=="__main__":
	#hex_to_int(r'fe')	
	#hex_to_int2(b'\xfe')	
	send('51 01')
	send('21 81')
	send('e1 81')
	send('21 41')
	send('51 01')
	


