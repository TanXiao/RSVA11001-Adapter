import sys
import struct

jffs2Magic = '\x85\x19'

f = open(sys.argv[1],'r');

data = f.read()

magicLocations = [data.find(jffs2Magic)]

while True:
	p = data.find(jffs2Magic,magicLocations[-1] + len(jffs2Magic))
	
	if(p == -1):
		break
		
	magicLocations.append(p)
	
	
#print([str(hex(i)) for i in magicLocations])
fmt = '<HHII'

direntfmt = fmt + 'IIIIBB2sII'
direntkeys = ['magic','nodetype','totlen','hdr_crc','pino','version','ino','mctime','nsize','type','unused','node_crc','name_crc']

inodefmt = fmt + 'IIIHHIIIIIIIBBhII'
inodekeys = ['magic','nodetype','totlen','hdr_crc','ino','version','mode','uid','gid','isize','atime','mtime','ctime','offset','csize','dsize','compr','usercompr','flags','data_crc','node_crc'] 

JFFS2_FEATURE_INCOMPAT = 0xC000
JFFS2_FEATURE_ACCURATE = 0x2000
JFFS2_COMPAT_MASK = 0xc000 | 0x2000
JFFS2_NODETYPE_INODE = 2
JFFS2_NODETYPE_DIRENT = 1

for start in magicLocations:
	
	

	magic,nodeType,nodeLength,nodeCrc = struct.unpack(fmt,data[start:start+12])
	
	nodeType = (~JFFS2_COMPAT_MASK) & nodeType
	
	print( hex(start) + ': type=' + hex(  nodeType) + ', length=' + str(nodeLength) + ', crc=' + str(nodeCrc))
	
	if(nodeType == JFFS2_NODETYPE_INODE):
		values = struct.unpack(inodefmt,data[start:start+68])
		values = list(values)
		keys = inodekeys
	elif nodeType == JFFS2_NODETYPE_DIRENT:
		values= struct.unpack(direntfmt,data[start:start+40])
		values = list(values)
		keys = direntkeys
	else:
		continue
		
	obj= dict(zip(keys,values))
		
	print(obj)
	
	
	
	
	
	
	
