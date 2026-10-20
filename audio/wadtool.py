import struct
def read_wad(path):
    d=open(path,'rb').read(); n,off=struct.unpack_from('<II',d,4); L=[]
    for i in range(n):
        p,sz,name=struct.unpack_from('<II8s',d,off+16*i); L.append((name.rstrip(b'\0').decode(),d[p:p+sz]))
    return d[:4],L
def lzss(data, window=4096, minm=3, maxm=18):
    # flag byte per 8 items: bit=1 literal, 0 = match (2 bytes: 12-bit distance-1, 4-bit len-3)
    out=bytearray(); i=0; n=len(data)
    import collections
    table=collections.defaultdict(list)
    while i<n:
        flagpos=len(out); out.append(0); flags=0
        for bit in range(8):
            if i>=n: break
            best=(0,0)
            key=data[i:i+3]
            if len(key)==3:
                for j in reversed(table[key]):
                    if i-j>window: break
                    l=0
                    while l<maxm and i+l<n and data[j+l]==data[i+l]: l+=1
                    if l>best[0]: best=(l,i-j)
                    if l==maxm: break
            if best[0]>=minm:
                l,dist=best; v=((dist-1)<<4)|(l-minm); out+=struct.pack('<H',v)
                for k in range(l):
                    if i+k+3<=n: table[data[i+k:i+k+3]].append(i+k)
                i+=l
            else:
                flags|=1<<bit; out.append(data[i])
                if i+3<=n: table[data[i:i+3]].append(i)
                i+=1
        out[flagpos]=flags
    return bytes(out)
def unlzss(c, n, window=4096, minm=3):
    out=bytearray(); i=0
    while len(out)<n:
        flags=c[i]; i+=1
        for bit in range(8):
            if len(out)>=n: break
            if flags&(1<<bit): out.append(c[i]); i+=1
            else:
                v=c[i]|(c[i+1]<<8); i+=2; dist=(v>>4)+1; l=(v&15)+minm
                for k in range(l): out.append(out[-dist])
    return bytes(out)
