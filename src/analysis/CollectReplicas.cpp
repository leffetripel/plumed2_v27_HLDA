/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2012-2017 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed.org for more information.

   This file is part of plumed, version 2.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "core/AverageBase.h"
#include "core/PlumedMain.h"
#include "core/ActionSet.h"
#include "core/ActionRegister.h"

namespace PLMD {
namespace analysis {

class CollectReplicas : public AverageBase {
private:
  unsigned ndata, nreplicas;
  std::vector<double> data, allweights;
  std::vector<std::vector<double> > alldata;
public:
  static void registerKeywords( Keywords& keys );
  explicit CollectReplicas( const ActionOptions& );
  void interpretDotStar( const std::string& ulab, unsigned& nargs, std::vector<Value*>& myvals );
  void accumulateData( const double& cweight );
  void runFinalJobs();
};

PLUMED_REGISTER_ACTION(CollectReplicas,"COLLECT_REPLICAS")

void CollectReplicas::registerKeywords( Keywords& keys ) {
  AverageBase::registerKeywords( keys ); ActionWithValue::useCustomisableComponents( keys );
  keys.add("optional","ARG","the data that you would like to collect to analyze later");
  keys.addOutputComponent("logweights","default","this value stores the logarithms of the weights of the stored configurations");
}

CollectReplicas::CollectReplicas( const ActionOptions& ao):
  Action(ao),
  AverageBase(ao),
  ndata(0),
  data(n_real_args)
{
  if( getPntrToArgument(0)->hasDerivatives() && getPntrToArgument(0)->getRank()>0 ) {
      error("cannot collect grid input for later analysis -- if you need this email gareth.tribello@gmail.com");
  } 
  // Get the number of replicas
  if(comm.Get_rank()==0) nreplicas=multi_sim_comm.Get_size();
  comm.Bcast(nreplicas,0);
  if( n_real_args>0 ) error("have not implemented collecting arguments from replicas yet");
  // Setup the components
  setupComponents( nreplicas );
}

void CollectReplicas::interpretDotStar( const std::string& ulab, unsigned& nargs, std::vector<Value*>& myvals ) {
  for(unsigned i=0; i<getNumberOfComponents(); ++i) copyOutput(i)->interpretDataRequest( ulab, nargs, myvals, "" );
}

void CollectReplicas::accumulateData( const double& cweight ) {
  unsigned nvals = getPntrToArgument(0)->getNumberOfValues( getLabel() ); 
  // Collect the bias from all replicas
  std::vector<double> biases(nreplicas,0.0), data_rep(nvals*(getNumberOfComponents()-1),0.0);
  if(comm.Get_rank()==0) multi_sim_comm.Allgather(cweight,biases);
  // Collect the data from all replicas
  for(unsigned j=0;j<getNumberOfComponents()-1;++j) {
      for(unsigned i=0;i<nvals;++i) data_rep[nvals*j+i] = getPntrToArgument(j)->get(i);
  }
  std::vector<double> datap(nreplicas*data_rep.size(),0.0);
  if(comm.Get_rank()==0) multi_sim_comm.Allgather(data_rep,datap);
  

  // Now accumulate average
  if( clearstride>0 ) {
      for(unsigned i=0;i<nvals;++i) {
          for(unsigned k=0; k<biases.size(); k++) {
              for(unsigned j=0;j<getNumberOfComponents()-1;++j) getPntrToOutput(j)->set( ndata, datap[k*data_rep.size()+nvals*j+i] );
              getPntrToOutput(getNumberOfComponents()-1)->set( ndata, biases[k] ); ndata++;
          }
      } 
      // Start filling the data set again from scratch
      if( getStep()%clearstride==0 ) { ndata=0; }
  } else {
      for(unsigned i=0;i<nvals;++i) {
          for(unsigned k=0; k<biases.size(); k++) {
              for(unsigned j=0;j<getNumberOfComponents()-1;++j) data[j] = datap[k*data_rep.size()+nvals*j+i];
              allweights.push_back( biases[k] ); alldata.push_back( data );
          }
      }
  }
}

void CollectReplicas::runFinalJobs() {
  transferCollectedDataToValue( alldata, allweights );
}

}
}