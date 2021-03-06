#include "BelosConfigDefs.hpp"
#include "BelosLinearProblem.hpp"
#include "BelosTpetraAdapter.hpp"
#include "BelosPseudoBlockCGSolMgr.hpp"
#include "MatrixMarket_Tpetra.hpp"
#include "Tpetra_Map.hpp"
#include "Tpetra_CrsMatrix.hpp"
#include "Tpetra_DefaultPlatform.hpp"
#include "Ifpack2_Preconditioner.hpp"
#include "Ifpack2_Hypre.hpp"
#include "Teuchos_CommandLineProcessor.hpp"
#include "Teuchos_ParameterList.hpp"
#include "Teuchos_StandardCatchMacros.hpp"

int main(int argc, char *argv[]) {
  using Teuchos::Array;
  using Teuchos::RCP;
  using Teuchos::rcp;
  using Teuchos::ParameterList;
  using Ifpack2::FunctionParameter;
  using Ifpack2::Hypre::Prec;

  // Specify types used in this example
  typedef Tpetra::CrsMatrix<>::scalar_type Scalar;
  typedef Tpetra::CrsMatrix<>::local_ordinal_type LO;
  typedef Tpetra::CrsMatrix<>::global_ordinal_type GO;
  typedef Tpetra::CrsMatrix<>::node_type Node;
  typedef Tpetra::DefaultPlatform::DefaultPlatformType Platform;
  typedef Tpetra::CrsMatrix<Scalar> CrsMatrix;
  typedef Tpetra::MultiVector<Scalar> MV;
  typedef Tpetra::Operator<Scalar> OP;
  typedef Ifpack2::Preconditioner<Scalar> Preconditioner;
  typedef Tpetra::Map<> Map;

  // Initialize the MPI session
  Teuchos::oblackholestream blackhole;
  Teuchos::GlobalMPISession mpiSession(&argc,&argv,&blackhole);

  // Get the default communicator and node
  Platform &platform = Tpetra::DefaultPlatform::getDefaultPlatform();
  RCP<const Teuchos::Comm<int> > comm = platform.getComm();
  RCP<Node> node = platform.getNode();

  // Get parameters from command-line processor
  int nx = 10;
  Scalar tol = 1e-6;
  bool verbose = false;
  Teuchos::CommandLineProcessor cmdp(false,true);
  cmdp.setOption("nx",&nx, "Number of mesh points in x direction.");
  cmdp.setOption("tolerance",&tol, "Relative residual used for solver.");
  cmdp.setOption("verbose","quiet",&verbose, "Whether to print a lot of info or a little bit.");
  if(cmdp.parse(argc,argv) != Teuchos::CommandLineProcessor::PARSE_SUCCESSFUL) {
    return -1;
  }

  // Create the row map
  int n = nx*nx;
  RCP<Map> map = rcp(new Map(n,0,comm));

  // Create the 2D Laplace operator
  RCP<CrsMatrix> A = rcp(new CrsMatrix(map,5));
  for(LO i = 0; i<nx; i++) {
    for(LO j = 0; j<nx; j++) {
      GO row = i*nx+j;
      if(!map->isNodeGlobalElement(row))
        continue;

      Array<LO> indices;
      Array<Scalar> values;

      if(i > 0) {
        indices.push_back(row - nx);
        values.push_back(-1.0);
      }
      if(i < nx-1) {
        indices.push_back(row + nx);
        values.push_back(-1.0);
      }
      indices.push_back(row);
      values.push_back(4.0);
      if(j > 0) {
        indices.push_back(row-1);
        values.push_back(-1.0);
      }
      if(j < nx-1) {
        indices.push_back(row+1);
        values.push_back(-1.0);
      }
      A->insertGlobalValues(row,indices,values);
    }
  }
  A->fillComplete();

  // Create the initial guess and right hand side
  RCP<MV> trueX = rcp(new MV(A->getRowMap(),1,false));
  RCP<MV> X = rcp(new MV(A->getRowMap(),1));
  RCP<MV> B = rcp(new MV(A->getRowMap(),1,false));
  trueX->randomize();
  A->apply(*trueX,*B);

  // Create the parameters for hypre
  RCP<FunctionParameter> functs[6];
  functs[0] = rcp(new FunctionParameter(Prec, &HYPRE_BoomerAMGSetPrintLevel, 1)); // print AMG solution info
  functs[1] = rcp(new FunctionParameter(Prec, &HYPRE_BoomerAMGSetCoarsenType, 6)); // Falgout coarsening
  functs[2] = rcp(new FunctionParameter(Prec, &HYPRE_BoomerAMGSetRelaxType, 6)); // Sym GS/Jacobi hybrid
  functs[3] = rcp(new FunctionParameter(Prec, &HYPRE_BoomerAMGSetNumSweeps, 1)); // Sweeps on each level
  functs[4] = rcp(new FunctionParameter(Prec, &HYPRE_BoomerAMGSetTol, 0.0)); // Conv tolerance zero
  functs[5] = rcp(new FunctionParameter(Prec, &HYPRE_BoomerAMGSetMaxIter, 1)); // Do only one iteration!

  // Create the preconditioner
  RCP<Preconditioner> prec = rcp(new Ifpack2::Ifpack2_Hypre<Scalar,LO,GO,Node>(A));
  ParameterList hypreList;
  hypreList.set("SolveOrPrecondition", Prec);
  hypreList.set("Preconditioner", Ifpack2::Hypre::BoomerAMG);
  hypreList.set("NumFunctions", 6);
  hypreList.set<RCP<FunctionParameter>*>("Functions", functs);
  prec->setParameters(hypreList);
  prec->compute();

  // Create the linear problem
  RCP< Belos::LinearProblem<Scalar,MV,OP> > problem = rcp(new Belos::LinearProblem<Scalar,MV,OP>(A,X,B));
  problem->setHermitian();
  problem->setLeftPrec(prec);
  problem->setProblem();

  // Create the parameter list
  RCP<ParameterList> belosList = rcp(new ParameterList());
  belosList->set("Convergence Tolerance", tol);
  if(verbose)
    belosList->set("Verbosity", Belos::Errors + Belos::Warnings + Belos::TimingDetails + Belos::StatusTestDetails);
  else
    belosList->set("Verbosity", Belos::Errors + Belos::Warnings);

  // Create the Belos linear solver
  RCP< Belos::SolverManager<Scalar,MV,OP> > newSolver = rcp(new Belos::PseudoBlockCGSolMgr<Scalar,MV,OP>(problem,belosList));

  // Perform solve
  newSolver->solve();
}
