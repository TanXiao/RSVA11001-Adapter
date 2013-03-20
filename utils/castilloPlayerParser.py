import sys
import struct

h264out = None
h264i =0 

wroteIFrame = False

with open(sys.argv[1],'r') as fin:
	while True:
		owspHeader = fin.read(8)
		
		if len ( owspHeader) < 8 :
			break
		
		owspLength, = struct.unpack_from(">I",owspHeader,0)
		owspSeqNum, = struct.unpack_from("<I",owspHeader,4)
		
		sys.stdout.write("OWSP Length:" + str(owspLength) + ", Sequence Number:" + str(owspSeqNum) +'\n')
		
		payloadLength = owspLength - 4
		
		payload = fin.read(payloadLength)
		
		offset = 0
		
		while True:
			if len(payload) - offset < 4:
				break
			tlvType, = struct.unpack_from("<H",payload,offset)
			tlvPacketLength, = struct.unpack_from("<H",payload,offset+2)
			
			
			sys.stdout.write("TLV Type:" + str(tlvType) + ", length:" + str(tlvPacketLength) +'\n')
			tlvPayloadStart = offset + 4
			tlvPayload = payload[tlvPayloadStart:tlvPayloadStart+tlvPacketLength]
			
			offset += tlvPacketLength + 4
			
			if tlvType == 65:
				if h264out:
					h264out.close()
					
				h264out = open( 'h264_' + str(h264i) + '.h264' ,'w')
				h264i += 1
				
			elif tlvType == 102 or tlvType == 100:
				
				if tlvType == 100 and not wroteIFrame:
					open('h264_iframe.out','w').write(tlvPayload)
					wroteIFrame = True
				if h264out:
					h264out.write(tlvPayload)
