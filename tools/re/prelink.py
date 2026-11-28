"""Parse the kernelcache __PRELINK_INFO plist (with ID/IDREF) into python objects."""
import re, struct, xml.etree.ElementTree as ET
def load(path='fw/kernelcache.n94.macho'):
    m=open(path,'rb').read()
    i=m.find(b'<dict><key>_PrelinkInfoDictionary</key>')
    if i<0: i=m.find(b'<dict>',m.find(b'_PrelinkInfoDictionary')-20)
    j = m.rfind(b'</dict>', i, m.find(b'\x00', i)) + 7
    root=ET.fromstring(m[i:j].decode('latin1'))
    ids={}
    def conv(e):
        if 'IDREF' in e.attrib: return ids[e.attrib['IDREF']]
        t=e.tag
        if t=='dict':
            ch=list(e); v={ch[k].text: conv(ch[k+1]) for k in range(0,len(ch),2)}
        elif t=='array': v=[conv(c) for c in e]
        elif t=='integer': v=int(e.text,0)
        elif t=='string': v=e.text or ''
        elif t=='true': v=True
        elif t=='false': v=False
        else: v=e.text
        if 'ID' in e.attrib: ids[e.attrib['ID']]=v
        return v
    return m, conv(root)
