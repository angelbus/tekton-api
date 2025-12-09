import React from 'react';
import { Database, Server, Cloud, Shield, Lock, Eye, ArrowRight, Zap, HardDrive, Activity, Settings, GitBranch } from 'lucide-react';

const FullGoldenGatePipeline = () => {
  const N = 5;
  
  return (
    <div className="w-full h-full bg-gradient-to-br from-slate-50 to-slate-100 p-8 overflow-auto">
      <div className="max-w-[1800px] mx-auto">
        
        {/* Title */}
        <div className="mb-6 text-center">
          <h1 className="text-3xl font-bold text-slate-800 mb-2">Oracle GoldenGate to AWS RDS - Full Data Pipeline</h1>
          <p className="text-slate-600">On-Premise to Cloud Multi-Tenant Replication Architecture</p>
        </div>
        
        {/* Main Layout: Two Columns */}
        <div className="grid grid-cols-2 gap-6">
          
          {/* ==================== LEFT: ON-PREMISE LAYER ==================== */}
          <div className="space-y-4">
            <div className="bg-slate-100 rounded-lg shadow-lg border-2 border-slate-500 p-6">
              <div className="flex items-center gap-3 mb-4">
                <Server className="text-slate-700" size={28} />
                <h3 className="text-xl font-bold text-slate-800">On-Premise Environment</h3>
              </div>
              
              {/* Oracle CDB */}
              <div className="bg-white rounded-lg p-6 border-2 border-slate-300 mb-4">
                <div className="flex items-center gap-3 mb-4">
                  <Database className="text-slate-700" size={24} />
                  <h4 className="font-bold text-slate-800">GOLDEN On-Prem (Oracle CDB)</h4>
                </div>
                
                <div className="bg-slate-700 text-white rounded-lg p-6 shadow-lg mb-4">
                  <Database className="mx-auto mb-3" size={48} />
                  <div className="text-center">
                    <div className="font-bold text-xl mb-2">Oracle Container Database</div>
                    <div className="text-sm opacity-90">Multi-Tenant Architecture</div>
                  </div>
                </div>
                
                {/* PDBs */}
                <div className="grid grid-cols-2 gap-3 mb-4">
                  <div className="bg-blue-100 rounded-lg p-3 border-2 border-blue-300">
                    <Database className="mx-auto mb-2 text-blue-700" size={24} />
                    <div className="text-center">
                      <div className="font-bold text-sm text-slate-800">Financial PDB</div>
                      <div className="text-xs text-slate-600 mt-1">Pluggable DB</div>
                    </div>
                  </div>
                  
                  <div className="bg-purple-100 rounded-lg p-3 border-2 border-purple-300">
                    <Database className="mx-auto mb-2 text-purple-700" size={24} />
                    <div className="text-center">
                      <div className="font-bold text-sm text-slate-800">Reporting PDB</div>
                      <div className="text-xs text-slate-600 mt-1">Pluggable DB</div>
                    </div>
                  </div>
                </div>
              </div>
              
              {/* GoldenGate On-Prem */}
              <div className="bg-orange-50 rounded-lg p-6 border-2 border-orange-300">
                <div className="flex items-center gap-3 mb-4">
                  <Zap className="text-orange-600" size={24} />
                  <h4 className="font-bold text-slate-800">Oracle GoldenGate (On-Prem)</h4>
                </div>
                
                <div className="space-y-3">
                  {/* Extract Process */}
                  <div className="bg-white rounded-lg p-4 border border-orange-200">
                    <div className="flex items-center gap-2 mb-2">
                      <div className="bg-orange-500 text-white rounded px-2 py-1 text-xs font-bold">
                        EXTRACT
                      </div>
                      <span className="text-sm font-semibold text-slate-800">Change Data Capture</span>
                    </div>
                    <div className="text-xs text-slate-600">
                      • Captures changes from CDB<br/>
                      • Reads redo logs<br/>
                      • Writes to trail files
                    </div>
                  </div>
                  
                  {/* Pump Process */}
                  <div className="bg-white rounded-lg p-4 border border-orange-200">
                    <div className="flex items-center gap-2 mb-2">
                      <div className="bg-orange-600 text-white rounded px-2 py-1 text-xs font-bold">
                        PUMP
                      </div>
                      <span className="text-sm font-semibold text-slate-800">Data Distribution</span>
                    </div>
                    <div className="text-xs text-slate-600">
                      • Routes to AWS targets<br/>
                      • Handles network transfer<br/>
                      • Manages trail shipping
                    </div>
                  </div>
                </div>
              </div>
              
              {/* Connectivity */}
              <div className="mt-4 bg-amber-50 rounded-lg p-4 border-2 border-amber-300">
                <div className="flex items-center gap-2 mb-2">
                  <Lock className="text-amber-700" size={20} />
                  <span className="font-bold text-slate-800 text-sm">Network Connectivity</span>
                </div>
                <div className="grid grid-cols-2 gap-2">
                  <div className="bg-white rounded p-2 border border-amber-200 text-center">
                    <div className="text-xs font-bold text-slate-800">Site-to-Site VPN</div>
                    <div className="text-xs text-slate-600">IPSec Tunnel</div>
                  </div>
                  <div className="bg-white rounded p-2 border border-amber-200 text-center">
                    <div className="text-xs font-bold text-slate-800">AWS Direct Connect</div>
                    <div className="text-xs text-slate-600">Dedicated Link</div>
                  </div>
                </div>
              </div>
            </div>
          </div>
          
          {/* ==================== RIGHT: AWS CLOUD LAYER ==================== */}
          <div className="space-y-4">
            <div className="bg-gradient-to-br from-orange-50 to-orange-100 rounded-lg shadow-lg border-2 border-orange-400 p-6">
              <div className="flex items-center gap-3 mb-4">
                <Cloud className="text-orange-600" size={28} />
                <h3 className="text-xl font-bold text-slate-800">AWS Landing Zone</h3>
              </div>
              
              {/* VPC Container */}
              <div className="bg-white rounded-lg p-6 border-2 border-blue-400 mb-4">
                <div className="flex items-center justify-between mb-4">
                  <div className="flex items-center gap-2">
                    <Shield className="text-blue-600" size={20} />
                    <span className="font-bold text-slate-800">VPC - Private Subnets</span>
                  </div>
                  <span className="text-xs bg-blue-100 text-blue-800 px-2 py-1 rounded font-semibold">
                    10.0.0.0/16
                  </span>
                </div>
                
                {/* Security Groups */}
                <div className="bg-blue-50 rounded-lg p-3 border border-blue-200 mb-4">
                  <div className="flex items-center gap-2 mb-2">
                    <Lock className="text-blue-600" size={16} />
                    <span className="text-xs font-bold text-slate-800">Security Groups</span>
                  </div>
                  <div className="text-xs text-slate-700 space-y-1">
                    <div>• TCP 1521 (Oracle)</div>
                    <div>• TCP 443 (Management)</div>
                    <div>• GoldenGate ports</div>
                  </div>
                </div>
                
                {/* GoldenGate Replicat on EC2 */}
                <div className="bg-orange-100 rounded-lg p-4 border-2 border-orange-300 mb-4">
                  <div className="flex items-center gap-2 mb-3">
                    <Server className="text-orange-700" size={20} />
                    <span className="font-bold text-slate-800 text-sm">GoldenGate Replicat (EC2)</span>
                  </div>
                  
                  <div className="grid grid-cols-2 gap-2">
                    <div className="bg-white rounded p-3 border border-blue-200">
                      <div className="bg-blue-600 text-white rounded px-2 py-1 text-xs font-bold mb-2 text-center">
                        REPLICAT-FIN
                      </div>
                      <div className="text-xs text-slate-600">
                        Applies to Financial RDS
                      </div>
                    </div>
                    
                    <div className="bg-white rounded p-3 border border-purple-200">
                      <div className="bg-purple-600 text-white rounded px-2 py-1 text-xs font-bold mb-2 text-center">
                        REPLICAT-REP
                      </div>
                      <div className="text-xs text-slate-600">
                        Applies to Reporting RDS
                      </div>
                    </div>
                  </div>
                </div>
                
                {/* Golden RDS Instances */}
                <div className="grid grid-cols-2 gap-3 mb-4">
                  <div className="bg-blue-50 rounded-lg p-4 border-2 border-blue-300">
                    <div className="bg-blue-700 text-white rounded-lg p-3 shadow-md border-2 border-yellow-400 mb-2">
                      <Database className="mx-auto mb-1" size={28} />
                      <div className="text-center">
                        <div className="font-bold text-sm">GOLDEN</div>
                        <div className="font-bold text-sm">Financial RDS</div>
                        <div className="text-xs mt-1 opacity-90">1 PDB</div>
                      </div>
                    </div>
                    <div className="text-xs text-center text-slate-600 bg-blue-100 rounded p-2">
                      Single-Tenant RDS
                    </div>
                  </div>
                  
                  <div className="bg-purple-50 rounded-lg p-4 border-2 border-purple-300">
                    <div className="bg-purple-700 text-white rounded-lg p-3 shadow-md border-2 border-yellow-400 mb-2">
                      <Database className="mx-auto mb-1" size={28} />
                      <div className="text-center">
                        <div className="font-bold text-sm">GOLDEN</div>
                        <div className="font-bold text-sm">Reporting RDS</div>
                        <div className="text-xs mt-1 opacity-90">1 PDB</div>
                      </div>
                    </div>
                    <div className="text-xs text-center text-slate-600 bg-purple-100 rounded p-2">
                      Single-Tenant RDS
                    </div>
                  </div>
                </div>
                
                {/* DB Subnet Groups */}
                <div className="bg-slate-100 rounded-lg p-3 border border-slate-300">
                  <div className="flex items-center gap-2 mb-2">
                    <GitBranch className="text-slate-600" size={16} />
                    <span className="text-xs font-bold text-slate-800">DB Subnet Groups</span>
                  </div>
                  <div className="grid grid-cols-3 gap-2">
                    <div className="bg-white rounded p-2 text-center">
                      <div className="text-xs font-mono text-slate-700">subnet-a</div>
                    </div>
                    <div className="bg-white rounded p-2 text-center">
                      <div className="text-xs font-mono text-slate-700">subnet-b</div>
                    </div>
                    <div className="bg-white rounded p-2 text-center">
                      <div className="text-xs font-mono text-slate-700">subnet-c</div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
        
        {/* ==================== REPLICATION ARROWS ==================== */}
        <div className="my-6 bg-orange-100 rounded-lg p-6 border-2 border-orange-300">
          <h3 className="font-bold text-slate-800 mb-4 text-center">Replication Data Flows</h3>
          
          <div className="grid grid-cols-2 gap-6">
            {/* Financial Flow */}
            <div className="bg-white rounded-lg p-4 border-2 border-blue-300">
              <div className="font-bold text-blue-800 mb-3 flex items-center gap-2">
                <div className="bg-blue-600 text-white rounded px-2 py-1 text-xs">FINANCIAL</div>
                Data Pipeline
              </div>
              <div className="flex items-center justify-between text-sm">
                <div className="text-center">
                  <div className="bg-slate-700 text-white rounded px-3 py-2 mb-1 text-xs font-bold">Financial PDB</div>
                  <div className="text-xs text-slate-600">On-Prem</div>
                </div>
                <ArrowRight className="text-orange-500" size={20} />
                <div className="text-center">
                  <div className="bg-orange-500 text-white rounded px-3 py-2 mb-1 text-xs font-bold">EXTRACT</div>
                  <div className="text-xs text-slate-600">CDC</div>
                </div>
                <ArrowRight className="text-orange-500" size={20} />
                <div className="text-center">
                  <div className="bg-orange-600 text-white rounded px-3 py-2 mb-1 text-xs font-bold">PUMP</div>
                  <div className="text-xs text-slate-600">Ship</div>
                </div>
                <ArrowRight className="text-blue-500" size={20} />
                <div className="text-center">
                  <div className="bg-blue-600 text-white rounded px-3 py-2 mb-1 text-xs font-bold">REPLICAT</div>
                  <div className="text-xs text-slate-600">Apply</div>
                </div>
                <ArrowRight className="text-blue-500" size={20} />
                <div className="text-center">
                  <div className="bg-blue-700 text-white rounded px-3 py-2 mb-1 text-xs font-bold">Financial RDS</div>
                  <div className="text-xs text-slate-600">Target</div>
                </div>
              </div>
            </div>
            
            {/* Reporting Flow */}
            <div className="bg-white rounded-lg p-4 border-2 border-purple-300">
              <div className="font-bold text-purple-800 mb-3 flex items-center gap-2">
                <div className="bg-purple-600 text-white rounded px-2 py-1 text-xs">REPORTING</div>
                Data Pipeline
              </div>
              <div className="flex items-center justify-between text-sm">
                <div className="text-center">
                  <div className="bg-slate-700 text-white rounded px-3 py-2 mb-1 text-xs font-bold">Reporting PDB</div>
                  <div className="text-xs text-slate-600">On-Prem</div>
                </div>
                <ArrowRight className="text-orange-500" size={20} />
                <div className="text-center">
                  <div className="bg-orange-500 text-white rounded px-3 py-2 mb-1 text-xs font-bold">EXTRACT</div>
                  <div className="text-xs text-slate-600">CDC</div>
                </div>
                <ArrowRight className="text-orange-500" size={20} />
                <div className="text-center">
                  <div className="bg-orange-600 text-white rounded px-3 py-2 mb-1 text-xs font-bold">PUMP</div>
                  <div className="text-xs text-slate-600">Ship</div>
                </div>
                <ArrowRight className="text-purple-500" size={20} />
                <div className="text-center">
                  <div className="bg-purple-600 text-white rounded px-3 py-2 mb-1 text-xs font-bold">REPLICAT</div>
                  <div className="text-xs text-slate-600">Apply</div>
                </div>
                <ArrowRight className="text-purple-500" size={20} />
                <div className="text-center">
                  <div className="bg-purple-700 text-white rounded px-3 py-2 mb-1 text-xs font-bold">Reporting RDS</div>
                  <div className="text-xs text-slate-600">Target</div>
                </div>
              </div>
            </div>
          </div>
        </div>
        
        {/* ==================== CLONE RDS INSTANCES LAYER ==================== */}
        <div className="bg-white rounded-lg shadow-lg border-2 border-green-400 p-6">
          <div className="flex items-center gap-3 mb-4">
            <Database className="text-green-700" size={28} />
            <h3 className="text-xl font-bold text-slate-800">Clone RDS Instances Layer (N = {N} per tenant)</h3>
          </div>
          
          {/* Financial Clones */}
          <div className="mb-6">
            <div className="flex items-center gap-3 mb-3">
              <div className="bg-blue-100 text-blue-800 px-4 py-2 rounded-lg font-bold text-sm">
                Financial Clone Array
              </div>
              <ArrowRight className="text-blue-500" size={20} />
              <span className="text-sm text-slate-600">Sourced from GOLDEN Financial RDS</span>
            </div>
            
            <div className="grid grid-cols-5 gap-3">
              {Array.from({ length: N }, (_, idx) => (
                <div key={`fin-${idx}`} className="bg-gradient-to-br from-blue-400 to-blue-600 rounded-lg p-3 shadow-md text-white text-center">
                  <div className="bg-blue-300 rounded-full w-10 h-10 mx-auto mb-2 flex items-center justify-center">
                    <Database size={20} />
                  </div>
                  <div className="font-bold text-xs">Financial</div>
                  <div className="font-bold text-xs">Clone #{idx + 1}</div>
                  <div className="text-xs opacity-90 mt-1">[{idx}]</div>
                </div>
              ))}
            </div>
          </div>
          
          {/* Reporting Clones */}
          <div>
            <div className="flex items-center gap-3 mb-3">
              <div className="bg-purple-100 text-purple-800 px-4 py-2 rounded-lg font-bold text-sm">
                Reporting Clone Array
              </div>
              <ArrowRight className="text-purple-500" size={20} />
              <span className="text-sm text-slate-600">Sourced from GOLDEN Reporting RDS</span>
            </div>
            
            <div className="grid grid-cols-5 gap-3">
              {Array.from({ length: N }, (_, idx) => (
                <div key={`rep-${idx}`} className="bg-gradient-to-br from-purple-400 to-purple-600 rounded-lg p-3 shadow-md text-white text-center">
                  <div className="bg-purple-300 rounded-full w-10 h-10 mx-auto mb-2 flex items-center justify-center">
                    <Database size={20} />
                  </div>
                  <div className="font-bold text-xs">Reporting</div>
                  <div className="font-bold text-xs">Clone #{idx + 1}</div>
                  <div className="text-xs opacity-90 mt-1">[{idx}]</div>
                </div>
              ))}
            </div>
          </div>
          
          {/* TCO Note */}
          <div className="mt-4 bg-green-50 rounded-lg p-4 border-2 border-green-300">
            <div className="flex items-start gap-3">
              <div className="bg-green-600 text-white rounded-full w-6 h-6 flex items-center justify-center flex-shrink-0">
                <span className="text-xs font-bold">$</span>
              </div>
              <div>
                <div className="font-bold text-green-900 mb-1">TCO Calculation</div>
                <div className="text-sm text-green-800">
                  Architecture supports <span className="font-bold">N clones per tenant</span>, but cost analysis uses 
                  <span className="font-bold"> N + 1</span> because AWS requires two GOLDEN RDS instances (Financial + Reporting).
                </div>
                <div className="mt-2 bg-green-100 rounded px-3 py-2 text-xs font-mono text-green-900">
                  Total RDS = 2 Golden + (N × 2) Clones = 2 + {N * 2} = <span className="font-bold">{2 + N * 2} instances</span>
                </div>
              </div>
            </div>
          </div>
        </div>
        
        {/* ==================== MANAGEMENT & OPERATIONS ==================== */}
        <div className="mt-6 grid grid-cols-3 gap-4">
          {/* CloudWatch */}
          <div className="bg-white rounded-lg shadow-md border-2 border-purple-300 p-4">
            <div className="flex items-center gap-2 mb-3">
              <Activity className="text-purple-600" size={24} />
              <h4 className="font-bold text-slate-800">CloudWatch Monitoring</h4>
            </div>
            <ul className="text-sm text-slate-600 space-y-1">
              <li>• RDS metrics & alarms</li>
              <li>• Replication lag tracking</li>
              <li>• CPU, memory, I/O</li>
              <li>• Custom dashboards</li>
            </ul>
          </div>
          
          {/* IAM */}
          <div className="bg-white rounded-lg shadow-md border-2 border-blue-300 p-4">
            <div className="flex items-center gap-2 mb-3">
              <Shield className="text-blue-600" size={24} />
              <h4 className="font-bold text-slate-800">IAM Roles & Policies</h4>
            </div>
            <ul className="text-sm text-slate-600 space-y-1">
              <li>• RDS access control</li>
              <li>• Lambda execution roles</li>
              <li>• Step Functions permissions</li>
              <li>• Snapshot automation</li>
            </ul>
          </div>
          
          {/* S3 */}
          <div className="bg-white rounded-lg shadow-md border-2 border-orange-300 p-4">
            <div className="flex items-center gap-2 mb-3">
              <HardDrive className="text-orange-600" size={24} />
              <h4 className="font-bold text-slate-800">S3 Storage</h4>
            </div>
            <ul className="text-sm text-slate-600 space-y-1">
              <li>• RDS snapshot backups</li>
              <li>• GoldenGate trail files</li>
              <li>• Audit logs storage</li>
              <li>• Lifecycle policies</li>
            </ul>
          </div>
        </div>
        
      </div>
    </div>
  );
};

export default FullGoldenGatePipeline;
