# 15 Data points test.

import bluetooth
import csv
import time
import sys
import json

def writeToFile( data1 ):

	#path1 = r"C:\Users\Jonah\OneDrive - Cornell University\Fall 2017\ECE 4760\Final Project\Server\ "
	try:
		with open('data15.csv', 'w',  newline='') as f:

			writer = csv.writer(f, delimiter=',')
			writer.writerow(data1) 
	except:
		print("Data not written")

def writeToFile1( data1 ):

	#path1 = r"C:\Users\Jonah\OneDrive - Cornell University\Fall 2017\ECE 4760\Final Project\Server\ "
	while True:
		try:
			with open('data15.txt', 'r+' ) as f:

				#writer = csv.writer(f, delimiter=',')
				#writer.writerow(data1) 
				f.seek(0,0)
				f.truncate()
				json.dump( data1, f)
			break
		except:
			#print("Data not written")
			break
			print( "Trying again..")

# Mac address of the HC-06:
macAddress = '98:d3:31:fd:31:27'
port = 1 

# Startup bluetooth connection:
print("Attempting to connect...")
s = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
s.connect((macAddress,port))
print("Connected!" )
time.sleep(0.5)

decodedData = ""
jsonData = {}
jsonData['sensorData'] = []

try:

	while True:
		time.sleep(0.02)
		data = s.recv(1024)
		#print( "Data:  " + str(data))
		val = len(data)
		if (data[val-1] == 10):

			decodedData = decodedData + data[0:val-1].decode('utf-8', "ignore")
			#print( "Decoded Data: " + str(decodedData) )
			rayData = str(decodedData).split()
			#dataRay =[]
			#print( "Data: ", str(decodedData).split())

			for i in range(15):
				decodedData = decodedData
				#l = 
				#p = rayData[0][0]
				num = rayData[i][:]
				jsonData['sensorData'].append({'Sensor' : 'Sensor' + str(i+1),  'R' : int(num)})
				#dataRay.append(num)

			timin = time.time()
			writeToFile1(jsonData)
			timEnd = time.time()
			sys.stdout.flush()
			print( "Json Data:" , jsonData)
			print("\n")
			print("Duration: " + str(timEnd-timin) + "\n")
			decodedData = ""
			jsonData = {}
			json.dumps(jsonData)
			jsonData['sensorData'] = []
			time.sleep(0.05)

		else:

			decodedData = decodedData + data.decode('utf-8', "ignore")
		#print("Data", data.decode('utf-8', "ignore"))

except Exception as e:
	print( "Exception:  " + str(e))
	pass

print("Disconnected")

s.close()