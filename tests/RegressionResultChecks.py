"""Checks for regression acceptance, independent of the native solver."""
import tempfile
import time
import unittest
import hashlib
from pathlib import Path
from run_geotechnical_regression import inspect_results,inspect_feature_results,inspect_analytic_head,inspect_static_convergence,inspect_analytic_displacement,accepted_static_dependency
from build_geotechnical_regression import write
from static_state_fingerprint import model_hashes
from seepage_convergence import inspect_seepage_convergence


class ResultChecks(unittest.TestCase):
    def test_finite_static_residual_warns_without_blocking(self):
        node={'type':'SlopeStabilityAnalysis','state':{'base_values':['']*6+['.0001']}}
        good='ISTEP= 1 IT= 4 FS=1.0000 Fmax= 1 ER= 7.8E-5 HG=1\n'
        bad='ISTEP= 2 IT= 20 FS=1.2000 Fmax= 1 ER= .0011894 HG=1\n'
        self.assertFalse(inspect_static_convergence(good,node)['errors'])
        result=inspect_static_convergence(good+bad,node)
        self.assertFalse(result['errors']);self.assertTrue(result['warnings'])
        self.assertTrue(inspect_static_convergence('ISTEP=1 IT=1 FS=1 Fmax=1 ER=NaN HG=1',node)['errors'])
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);self.started=time.time()

    def tearDown(self):self.temp.cleanup()

    def result(self,body):
        (self.root/'IIDISP.TXT').write_text('1 1 2\nresult\nnode values\n'+body,encoding='ascii')

    def test_head_and_pressure_have_different_units(self):
        self.result('1 177.52 1700000\n')
        result=inspect_results(self.root,self.started,True)
        self.assertEqual(result['errors'],[])
        self.assertEqual(result['displacements'][0]['max_abs_m'],177.52)

    def test_linear_seepage_initialization_is_not_nonlinear_convergence(self):
        initial=self.root/'SEEPFREE';initial.mkdir();key=['']*24;key[2]='21 2';key[11]='1 2 0 0 0 0.001';key[23]='0 0 0 0 0'
        (initial/'IKEY.DAT').write_text('\n'.join(key),encoding='ascii')
        (initial/'IIDISP.TXT').write_text('1 1 2\nhead\nJD HEAD P\n1 10 98100\n',encoding='ascii')
        log=f'WORKING DIR=\n\n{initial}\nIT=1 tol=1e-3 ERR=0.5 IFHG=1\n'
        self.assertTrue(inspect_seepage_convergence(log,self.root,self.started,True)['errors'])
        log+=f'WORKING DIR={self.root/"SEEPCASE01"}\nIT=3 tol=1e-3 ERR=0.0001 IFHG=1\n'
        self.assertFalse(inspect_seepage_convergence(log,self.root,self.started,True)['errors'])
        bad=log+'IT=4 tol=1e-3 ERR=0.01 IFHG=1\n'
        result=inspect_seepage_convergence(bad,self.root,self.started,True)
        self.assertFalse(result['errors']);self.assertTrue(result['warnings'])
        key[11]='1 2 0 300 0 0.001';(initial/'IKEY.DAT').write_text('\n'.join(key),encoding='ascii')
        self.assertTrue(inspect_seepage_convergence(log,self.root,self.started,True)['warnings'])
        key[11]='1 2 0 0 0 0.001';key[23]='2 0 0 0 0';(initial/'IKEY.DAT').write_text('\n'.join(key),encoding='ascii')
        self.assertTrue(inspect_seepage_convergence(log,self.root,self.started,True)['warnings'])
        (initial/'IKEY.DAT').unlink()
        self.assertTrue(inspect_seepage_convergence(log,self.root,self.started,True)['warnings'])
        self.assertTrue(inspect_seepage_convergence(log+'tol=1e-3 ERR=NaN IFHG=1\n',self.root,self.started,True)['errors'])

    def test_nonfinite_pressure_is_rejected(self):
        self.result('1 177.52 NaN\n')
        self.assertTrue(inspect_results(self.root,self.started,True)['errors'])

    def test_large_2d_displacement_is_rejected(self):
        self.result('1 11 0\n')
        self.assertTrue(inspect_results(self.root,self.started)['errors'])

    def test_static_output_does_not_satisfy_wave(self):
        self.result('1 0.001 0\n')
        result=inspect_feature_results(self.root,self.started,{'state':{'ifwave':'1'}})
        self.assertEqual(len(result['errors']),2)

    def test_empty_mode_file_is_rejected(self):
        (self.root/'IEIGEN.TXT').touch()
        result=inspect_feature_results(self.root,self.started,{'state':{'ifmode':'1'}})
        self.assertTrue(result['errors'])

    def test_real_positive_frequency_is_accepted(self):
        (self.root/'IEIGEN.TXT').write_text('1 eigenvalues\n\nI lambda w f T gamma beta A\n1 4 2 0.3183 3.14159 1 1 1\n',encoding='ascii')
        result=inspect_feature_results(self.root,self.started,{'state':{'ifmode':'1'}})
        self.assertFalse(result['errors'])
        self.assertEqual(result['artifacts'][0]['frequency_hz'],[0.3183])

    def test_beam_rotation_is_not_reported_in_metres(self):
        (self.root/'IIIN.TXT').write_text('2 2 1 2 5 1 0 0 0\n7 2 0 2 0 0 0 0\n0 0 1\n2 0 2\n1 1 2 21 3 1 0 0\n',encoding='ascii')
        self.result('1 0 0 0\n2 0 .001 20\n')
        result=inspect_results(self.root,self.started)
        self.assertFalse(result['errors']);self.assertEqual(result['displacements'][0]['max_abs_m'],.001)

    def test_historical_static_dependency_requires_exact_provenance(self):
        src=self.root/'project/source/计算输入';src.mkdir(parents=True)
        (src/'IIIN.DAT').write_text('2 2 1 2 5 1 0 0 0\n8 2 0 2 0 0 0 0\n0 0 1\n2 0 2\n1 1 2 21 3 1 0 0\n',encoding='ascii')
        for name in ['IIID.DAT','IMAT.DAT']:(src/name).write_text('accepted model\n',encoding='ascii')
        report=self.root/'accepted.json';raw={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in src.iterdir()}
        write(report,{'cases':[{'id':'D12','passed':True,'attempts':[{'phase':'analysis','passed':True,'input_sha256':raw}]}]})
        files=[]
        for folder in ['DISK00','CASE01WETT']:
            state=self.root/'project/restart'/folder;state.mkdir(parents=True);state_hashes={}
            for name in ['IIIN.DAT','IIID.DAT','IMAT.DAT','ISGMAR.TXT','IIICMM.TXT','IIDISP.TXT']:
                path=state/name;path.write_text('accepted state\n',encoding='ascii');digest=hashlib.sha256(path.read_bytes()).hexdigest()
                files.append({'copy':str(path),'sha256':digest});state_hashes[name]=digest
            write(state/'STATIC_STATE.json',{'version':2,'modelInputs':model_hashes(src),'stateFiles':state_hashes})
        provenance=self.root/'provenance.json';write(provenance,{'source_case':'D12','accepted_source_report':str(report),'files':files})
        project={'directory':'project','cases':[{'id':'D12','title':'source','result_status':'failed'}]}
        case={'title':'restart','accepted_static_dependency':{'id':'D12','report':str(report),'provenance':str(provenance)}}
        self.assertTrue(accepted_static_dependency(self.root,project,case,'D12'))
        changed=self.root/'project/restart/CASE01WETT/ISGMAR.TXT';changed.write_text('changed state',encoding='ascii')
        self.assertFalse(accepted_static_dependency(self.root,project,case,'D12'))
        changed.write_text('accepted state\n',encoding='ascii');(src/'IIID.DAT').write_text('changed constraint',encoding='ascii')
        self.assertFalse(accepted_static_dependency(self.root,project,case,'D12'))

    def test_beam_deflection_and_rotation_are_checked_independently(self):
        folder=self.root/'CASE01';folder.mkdir()
        (folder/'IIIN.TXT').write_text('2 2 1 2 5 1 0 0 0\n7 2 0 2 0 0 0 0\n0 0 1\n2 0 2\n1 1 2 21 3 1 0 0\n',encoding='ascii')
        resultfile=folder/'IIDISP.TXT'
        resultfile.write_text('1 2 3\nbeam\nJD UX UY THETA\n1 0 0 0\n2 0 .0001 .0001\n',encoding='ascii')
        definition={'field':'planar_beam','loading':'moment','load':1000,'length_x_m':2,
            'flexural_rigidity_Nm2':2.e7,'tolerance_m':1.e-9,'tolerance_rad':1.e-9}
        self.assertFalse(inspect_analytic_displacement(self.root,self.started,definition)['errors'])
        resultfile.write_text(resultfile.read_text().replace('.0001 .0001','.0001 .0002'),encoding='ascii')
        result=inspect_analytic_displacement(self.root,self.started,definition)
        self.assertEqual(result['metrics']['max_error_m'],0)
        self.assertIn('analytic rotation error exceeds tolerance',result['errors'])

    def test_thermal_gradient_checks_transverse_components(self):
        folder=self.root/'CASE01';folder.mkdir()
        (folder/'IIIN.TXT').write_text('2 3 0 20 5 1 0 0 0\n7 3 0 2 0 1 0 1\n0 0 0 1\n2 1 1 2\n',encoding='ascii')
        resultfile=folder/'IIDISP.TXT'
        resultfile.write_text('1 2 3\nthermal gradient\nJD UX UY UZ\n1 0 0 0\n2 .0002 -.0002 .0001\n',encoding='ascii')
        definition={'field':'thermal_gradient','gradient_axis':1,'strain_gradient_per_m':.0001,
            'length_x_m':2,'tolerance_m':1.e-10}
        self.assertFalse(inspect_analytic_displacement(self.root,self.started,definition)['errors'])
        resultfile.write_text(resultfile.read_text().replace('-.0002','0'),encoding='ascii')
        self.assertTrue(inspect_analytic_displacement(self.root,self.started,definition)['errors'])

    def test_analytic_darcy_head(self):
        folder=self.root/'SEEPCASE01';folder.mkdir()
        (folder/'IIIN.TXT').write_text('2 2 0 4 5 1 4 4 2\n8 2 0 100 0 10 0 0\n0 0 1\n100 0 2\n',encoding='ascii')
        (folder/'IIDISP.TXT').write_text('1 2 2\nhead pressure\nJD HEAD P\n1 20 1000\n2 15 1000\n',encoding='ascii')
        result=inspect_analytic_head(self.root,self.started,{'intercept_m':20,'gradient_x':-.05,'tolerance_m':.001})
        self.assertFalse(result['errors']);self.assertEqual(result['metrics']['max_error_m'],0)
        result=inspect_analytic_head(self.root,self.started,{'intercept_m':20,'gradient_x':-.1,'tolerance_m':.001})
        self.assertTrue(result['errors'])


if __name__=='__main__':unittest.main()
