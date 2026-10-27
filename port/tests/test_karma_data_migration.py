"""Karma records must accompany saves when the launcher changes data folders."""
from pathlib import Path
import sys
import tempfile
import unittest
from paths import ROOT
sys.path.insert(0,str(ROOT/'launcher'))
import userdata

class KarmaDataMigration(unittest.TestCase):
    def test_copies_all_tracking_without_mutating_source_or_copying_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            old=Path(temp)/'old';new=Path(temp)/'new'
            payload={'savedata/1/CUSA00223/infamousssUser0/dis.dat':b'save',
                **{f'{name}/record.bin':name.encode() for name in userdata.KARMA_DATA}}
            for name,data in payload.items():
                p=old/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
            (old/'cache').mkdir();(old/'cache/skip').write_bytes(b'cache')
            self.assertEqual(userdata.migration_size(old),sum(map(len,payload.values())))
            result=userdata.migrate(old,new)
            self.assertEqual(set(result['copied']),{'savedata',*userdata.KARMA_DATA})
            for name,data in payload.items():
                self.assertEqual((new/name).read_bytes(),data)
                self.assertEqual((old/name).read_bytes(),data)
            self.assertFalse((new/'cache').exists())

    def test_existing_destination_tracking_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as temp:
            old=Path(temp)/'old';new=Path(temp)/'new'
            for root,value in [(old,b'old'),(new,b'keep')]:
                p=root/'karma-saves-v2/record.bin';p.parent.mkdir(parents=True);p.write_bytes(value)
            result=userdata.migrate(old,new)
            self.assertIn('karma-saves-v2',result['skipped'])
            self.assertEqual((new/'karma-saves-v2/record.bin').read_bytes(),b'keep')

if __name__=='__main__':unittest.main()
