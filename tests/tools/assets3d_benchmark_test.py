import copy
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from assets3d_benchmark import compare
from benchmark_3d import stats

def report(index):
    pair=index//2+1; variant=('before','after') if pair%2 else ('after','before')
    h='a'*64
    meta=dict(pair=pair,order=index+1,variant=variant[index%2],runId=str(index),revision='base',
        platform='synthetic',runtime='mock',buildOptions={},videoMode={},emulatorConfigSha256=h,
        hashes=dict(elf=h,sources=h,harness=h,bench=h,moduleSelection=h,assets={'scene.glb':h}),
        workScope='peak',batchScope='sum',frameScope='all',ticksPerMs=1000)
    requests=[dict(warmup=j==0,frames=2,peakTicks=3000,totalTicks=4000,readingFrames=1,progress=[None,None]) for j in range(3)]
    results=[dict(name=str(i),status='completed',warmup=1,samples=2,iterations=1,metadata={},
        sampleTicks=[3000]*2,sampleMs=[3]*2,**stats([3]*2),
        batchCpu=dict(sampleMs=[4]*2,**stats([4]*2)),metrics=dict(requests=copy.deepcopy(requests))) for i in range(4)]
    frame=dict(sampleMs=[5]*36,overBudget=0,**stats([5]*36))
    return dict(version=2,status='completed',label='assets3d-e03',metadata=meta,
        frameBudgetMs=1000/60,results=results,frames=dict(cpu=frame,wall=copy.deepcopy(frame)))

class Comparison(unittest.TestCase):
    def setUp(self): self.reports=[report(i) for i in range(6)]
    def test_valid_variable_frames(self): self.assertEqual(len(compare(self.reports)['pairs']),3)
    def test_incomplete_frames(self):
        self.reports[0]['frames']['cpu']['sampleMs'].pop()
        with self.assertRaises(ValueError):compare(self.reports)
    def test_filtered_progress(self):
        self.reports[0]['results'][0]['metrics']['requests'][0]['progress'].pop()
        with self.assertRaises(ValueError):compare(self.reports)
    def test_peak_disagrees(self):
        self.reports[0]['results'][0]['metrics']['requests'][1]['peakTicks']=2000
        with self.assertRaises(ValueError):compare(self.reports)
    def test_changed_fixture(self):
        self.reports[0]['metadata']['hashes']['assets']['scene.glb']='b'*64
        with self.assertRaises(ValueError):compare(self.reports)

if __name__=='__main__':unittest.main()
