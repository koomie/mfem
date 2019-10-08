//                       MFEM Example 3 - Parallel Version
//
// Compile with: make ex3p
//
// Sample runs:  mpirun -np 4 ex3p -m ../data/star.mesh
//               mpirun -np 4 ex3p -m ../data/square-disc.mesh -o 2
//               mpirun -np 4 ex3p -m ../data/beam-tet.mesh
//               mpirun -np 4 ex3p -m ../data/beam-hex.mesh
//               mpirun -np 4 ex3p -m ../data/escher.mesh
//               mpirun -np 4 ex3p -m ../data/escher.mesh -o 2
//               mpirun -np 4 ex3p -m ../data/fichera.mesh
//               mpirun -np 4 ex3p -m ../data/fichera-q2.vtk
//               mpirun -np 4 ex3p -m ../data/fichera-q3.mesh
//               mpirun -np 4 ex3p -m ../data/square-disc-nurbs.mesh
//               mpirun -np 4 ex3p -m ../data/beam-hex-nurbs.mesh
//               mpirun -np 4 ex3p -m ../data/amr-quad.mesh -o 2
//               mpirun -np 4 ex3p -m ../data/amr-hex.mesh
//               mpirun -np 4 ex3p -m ../data/star-surf.mesh -o 2
//               mpirun -np 4 ex3p -m ../data/mobius-strip.mesh -o 2 -f 0.1
//               mpirun -np 4 ex3p -m ../data/klein-bottle.mesh -o 2 -f 0.1
//
// Description:  This example code solves a simple electromagnetic diffusion
//               problem corresponding to the second order definite Maxwell
//               equation curl curl E + E = f with boundary condition
//               E x n = <given tangential field>. Here, we use a given exact
//               solution E and compute the corresponding r.h.s. f.
//               We discretize with Nedelec finite elements in 2D or 3D.
//
//               The example demonstrates the use of H(curl) finite element
//               spaces with the curl-curl and the (vector finite element) mass
//               bilinear form, as well as the computation of discretization
//               error when the exact solution is known. Static condensation is
//               also illustrated.
//
//               We recommend viewing examples 1-2 before viewing this example.

#include "mfem.hpp"
#include <fstream>
#include <iostream>

#include "ddmesh.hpp"
#include "ddoper.hpp"

#include "testStrumpack.hpp"

using namespace std;
using namespace mfem;

// Exact solution, E, and r.h.s., f. See below for implementation.
double radiusFunction(const Vector &);
void E_exact(const Vector &, Vector &);
void f_exact(const Vector &, Vector &);
double freq = 1.0, kappa;
int dim;

#ifdef AIRY_TEST
//#define SIGMAVAL -10981.4158900991
//#define SIGMAVAL -1601.0
//#define SIGMAVAL -1009.0
#define SIGMAVAL -211.0
//#define SIGMAVAL -2.0
#else
//#define SIGMAVAL -2.0
#define SIGMAVAL -6007.0
//#define SIGMAVAL -191.0
//#define SIGMAVAL -1009.0
//#define SIGMAVAL -511.0
//#define SIGMAVAL -6007.0
#endif

void test1_RHS_exact(const Vector &x, Vector &f)
{
  const double kappa = M_PI;
  const double sigma = SIGMAVAL;
  f(0) = (sigma + kappa * kappa) * sin(kappa * x(1));
  f(1) = (sigma + kappa * kappa) * sin(kappa * x(2));
  f(2) = (sigma + kappa * kappa) * sin(kappa * x(0));
}

void test2_RHS_exact(const Vector &x, Vector &f)
{
#ifdef AIRY_TEST
  f = 0.0;
#else
  const double pi = M_PI;
  const double sigma = SIGMAVAL;
  const double c = (2.0 * pi * pi) + sigma;

  f(0) = c * sin(pi * x(1)) * sin(pi * x(2));
  f(1) = c * sin(pi * x(2)) * sin(pi * x(0));
  f(2) = c * sin(pi * x(0)) * sin(pi * x(1));
#endif
  
  /*  
  f(0) = SIGMAVAL * x(1) * (1.0 - x(1)) * x(2) * (1.0 - x(2));
  f(1) = SIGMAVAL * x(0) * (1.0 - x(0)) * x(2) * (1.0 - x(2));
  f(2) = SIGMAVAL * x(1) * (1.0 - x(1)) * x(0) * (1.0 - x(0));

  f(0) += 2.0 * ((x(1) * (1.0 - x(1))) + (x(2) * (1.0 - x(2))));
  f(1) += 2.0 * ((x(0) * (1.0 - x(0))) + (x(2) * (1.0 - x(2))));
  f(2) += 2.0 * ((x(0) * (1.0 - x(0))) + (x(1) * (1.0 - x(1))));
  */
  /*
  f(0) = SIGMAVAL * x(0) * x(1) * (1.0 - x(1)) * x(2) * (1.0 - x(2));
  f(1) = SIGMAVAL * x(1) * x(0) * (1.0 - x(0)) * x(2) * (1.0 - x(2));
  f(2) = SIGMAVAL * x(2) * x(1) * (1.0 - x(1)) * x(0) * (1.0 - x(0));

  f(0) += ((x(1) * (1.0 - x(1))) + (x(2) * (1.0 - x(2))));
  f(1) += ((x(0) * (1.0 - x(0))) + (x(2) * (1.0 - x(2))));
  f(2) += ((x(0) * (1.0 - x(0))) + (x(1) * (1.0 - x(1))));
  */
}

void TestHypreRectangularSerial()
{
  const int num_loc_cols = 100;
  const int num_loc_rows = 2 * num_loc_cols;
  const int nnz = num_loc_cols;
  
  HYPRE_Int rowStarts2[2];
  HYPRE_Int colStarts2[2];
  
  rowStarts2[0] = 0;
  rowStarts2[1] = num_loc_rows;

  colStarts2[0] = 0;
  colStarts2[1] = num_loc_cols;
  
  int *I_nnz = new int[num_loc_rows + 1];
  HYPRE_Int *J_col = new HYPRE_Int[nnz];

  I_nnz[0] = 0;

  // row 0: 0
  // row 1: 1
  // row 2: 0
  // ...

  // I_nnz row 0: 0
  // I_nnz row 1: 0
  // I_nnz row 2: 1
  // I_nnz row 3: 1
  // I_nnz row 4: 2
  // ...

  for (int i=0; i<num_loc_cols; ++i)
    {
      I_nnz[(2*i)+1] = i;
      I_nnz[(2*i)+2] = i+1;

      J_col[i] = i;
    }

  Vector diag(nnz);
  diag = 1.0;
  
  HypreParMatrix *A = new HypreParMatrix(MPI_COMM_WORLD, num_loc_rows, num_loc_rows, num_loc_cols, I_nnz, J_col, diag.GetData(), rowStarts2, colStarts2);

  Vector x(num_loc_cols);
  Vector y(num_loc_rows);

  x = 1.0;
  y = 0.0;

  cout << "Hypre serial test x norm " << x.Norml2() << endl;
  
  A->Mult(x, y);

  cout << "Hypre serial test y norm " << y.Norml2() << endl;

  delete I_nnz;
  delete J_col;
  delete A;
}

void TestHypreIdentity(MPI_Comm comm)
{
  int num_loc_rows = 100;
  HYPRE_Int size = 200;
  
  int nsdprocs, sdrank;
  MPI_Comm_size(comm, &nsdprocs);
  MPI_Comm_rank(comm, &sdrank);

  int *all_num_loc_rows = new int[nsdprocs];
		    
  MPI_Allgather(&num_loc_rows, 1, MPI_INT, all_num_loc_rows, 1, MPI_INT, comm);

  int sumLocalSizes = 0;

  for (int i=0; i<nsdprocs; ++i)
    sumLocalSizes += all_num_loc_rows[i];

  MFEM_VERIFY(size == sumLocalSizes, "");
  
  HYPRE_Int *rowStarts = new HYPRE_Int[nsdprocs+1];
  HYPRE_Int *rowStarts2 = new HYPRE_Int[2];
  rowStarts[0] = 0;
  for (int i=0; i<nsdprocs; ++i)
    rowStarts[i+1] = rowStarts[i] + all_num_loc_rows[i];

  const int osj = rowStarts[sdrank];

  rowStarts2[0] = rowStarts[sdrank];
  rowStarts2[1] = rowStarts[sdrank+1];
  
  int *I_nnz = new int[num_loc_rows + 1];
  HYPRE_Int *J_col = new HYPRE_Int[num_loc_rows];

  for (int i=0; i<num_loc_rows + 1; ++i)
    I_nnz[i] = i;

  for (int i=0; i<num_loc_rows; ++i)
    J_col[i] = osj + i;

  Vector diag(num_loc_rows);
  diag = 1.0;
  
  HypreParMatrix *A = new HypreParMatrix(comm, num_loc_rows, size, size, I_nnz, J_col, diag.GetData(), rowStarts2, rowStarts2);

  /*
  {
    HypreParMatrix *B = new HypreParMatrix(comm, num_loc_rows, size, size, I_nnz, J_col, diag.GetData(), rowStarts2, rowStarts2);
    HypreParMatrix *C = ParAdd(A, B);

    A->Print("IA");
    B->Print("IB");
    C->Print("IC");
  }
  */
  
  Vector x(num_loc_rows);
  Vector y(num_loc_rows);

  x = 1.0;
  y = 0.0;
  
  A->Mult(x, y);

  cout << sdrank << ": Hypre test y norm " << y.Norml2() << endl;
  
  delete I_nnz;
  delete J_col;
  delete rowStarts;
  delete rowStarts2;
  delete all_num_loc_rows;
  delete A;
}

void VisitTestPlotParMesh(const std::string filename, ParMesh *pmesh, const int ifId, const int myid)
{
  if (pmesh == NULL)
    return;

  DataCollection *dc = NULL;
  bool binary = false;
  if (binary)
    {
#ifdef MFEM_USE_SIDRE
      dc = new SidreDataCollection(filename.c_str(), pmesh);
#else
      MFEM_ABORT("Must build with MFEM_USE_SIDRE=YES for binary output.");
#endif
    }
  else
    {
      dc = new VisItDataCollection(filename.c_str(), pmesh);
      dc->SetPrecision(8);
      // To save the mesh using MFEM's parallel mesh format:
      // dc->SetFormat(DataCollection::PARALLEL_FORMAT);
    }

  // Define a grid function just to verify it is plotted correctly.
  H1_FECollection h1_coll(1, pmesh->Dimension());
  ParFiniteElementSpace fespace(pmesh, &h1_coll);

  if (ifId >= 0)
    cout << myid << ": interface " << ifId << " VISIT TEST: true V size " << fespace.GetTrueVSize() << ", V size " << fespace.GetVSize() << endl;
  
  ParGridFunction x(&fespace);
  FunctionCoefficient radius(radiusFunction);
  x.ProjectCoefficient(radius);
  
  dc->RegisterField("radius", &x);
  dc->SetCycle(0);
  dc->SetTime(0.0);
  dc->Save();

  delete dc;
}

void PrintDenseMatrixOfOperator(Operator const& op, const int nprocs, const int rank)
{
  const int n = op.Height();

  int ng = 0;
  MPI_Allreduce(&n, &ng, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  
  std::vector<int> alln(nprocs);
  MPI_Allgather(&n, 1, MPI_INT, alln.data(), 1, MPI_INT, MPI_COMM_WORLD);
  
  int myos = 0;

  int cnt = 0;
  for (int i=0; i<nprocs; ++i)
    {
      if (i < rank)
	myos += alln[i];

      cnt += alln[i];
    }

  MFEM_VERIFY(cnt == ng, "");
  
  Vector x(n);
  Vector y(n);

  /*
       Vector ej(Ndd);
     Vector Aej(Ndd);
     DenseMatrix ddd(Ndd);
     
     for (int j=0; j<Ndd; ++j)
       {
	 cout << "Computing column " << j << " of " << Ndd << " of ddi" << endl;
	 
	 ej = 0.0;
	 ej[j] = 1.0;
	 ddi.Mult(ej, Aej);

	 for (int i=0; i<Ndd; ++i)
	   {
	     ddd(i,j) = Aej[i];
	   }
       }
  */
}

int main(int argc, char *argv[])
{
   // 1. Initialize MPI.
   int num_procs, myid;
   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
   MPI_Comm_rank(MPI_COMM_WORLD, &myid);

   // 2. Parse command-line options.
   //const char *mesh_file = "../data/beam-tet.mesh";
#ifdef AIRY_TEST
   const char *mesh_file = "../data/inline-tetHalf.mesh";
#else
   const char *mesh_file = "../data/inline-tet.mesh";
#endif

   int order = 2;
   bool static_cond = false;
   bool visualization = 1;
   bool visit = false;
#ifdef MFEM_USE_STRUMPACK
   bool use_strumpack = false;
#endif

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree).");
   args.AddOption(&freq, "-f", "--frequency", "Set the frequency for the exact"
                  " solution.");
   args.AddOption(&static_cond, "-sc", "--static-condensation", "-no-sc",
                  "--no-static-condensation", "Enable static condensation.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
#ifdef MFEM_USE_STRUMPACK
   args.AddOption(&use_strumpack, "-strumpack", "--strumpack-solver",
                  "-no-strumpack", "--no-strumpack-solver",
                  "Use STRUMPACK's double complex linear solver.");
#endif

   args.Parse();
   if (!args.Good())
   {
      if (myid == 0)
      {
         args.PrintUsage(cout);
      }
      MPI_Finalize();
      return 1;
   }
   if (myid == 0)
   {
      args.PrintOptions(cout);
   }
   kappa = freq * M_PI;

   // 3. Read the (serial) mesh from the given mesh file on all processors.  We
   //    can handle triangular, quadrilateral, tetrahedral, hexahedral, surface
   //    and volume meshes with the same code.
   Mesh *mesh = new Mesh(mesh_file, 1, 1);
   dim = mesh->Dimension();
   int sdim = mesh->SpaceDimension();

   // 4. Refine the serial mesh on all processors to increase the resolution. In
   //    this example we do 'ref_levels' of uniform refinement. We choose
   //    'ref_levels' to be the largest number that gives a final mesh with no
   //    more than 1,000 elements.
   {
      int ref_levels =
	(int)floor(log(10000./mesh->GetNE())/log(2.)/dim);  // h = 0.0701539, 1/16
	//(int)floor(log(100000./mesh->GetNE())/log(2.)/dim);  // h = 0.0350769, 1/32
	//(int)floor(log(1000000./mesh->GetNE())/log(2.)/dim);  // h = 0.0175385, 1/64
	//(int)floor(log(10000000./mesh->GetNE())/log(2.)/dim);  // h = 0.00876923, 1/128
	//(int)floor(log(100000000./mesh->GetNE())/log(2.)/dim);  // exceeds memory with slab subdomains, first-order
      
      //(int)floor(log(100000./mesh->GetNE())/log(2.)/dim);
      for (int l = 0; l < ref_levels; l++)
      {
         mesh->UniformRefinement();
      }
   }

   // 4.5. Partition the mesh in serial, to define subdomains.
   // Note that the mesh attribute is overwritten here for convenience, which is bad if the attribute is needed.
   int nxyzSubdomains[3] = {1, 1, 2};
   const int numSubdomains = nxyzSubdomains[0] * nxyzSubdomains[1] * nxyzSubdomains[2];
   {
     int *subdomain = mesh->CartesianPartitioning(nxyzSubdomains);
     for (int i=0; i<mesh->GetNE(); ++i)  // Loop over all elements, to set the attribute as the subdomain index.
       {
	 mesh->SetAttribute(i, subdomain[i]+1);
       }
     delete subdomain;
   }

   if (myid == 0)
     {
       cout << "Subdomain partition " << nxyzSubdomains[0] << ", " << nxyzSubdomains[1] << ", " << nxyzSubdomains[2] << endl;
     }
   
   // 5. Define a parallel mesh by a partitioning of the serial mesh. Refine
   //    this mesh further in parallel to increase the resolution. Once the
   //    parallel mesh is defined, the serial mesh can be deleted. Tetrahedral
   //    meshes need to be reoriented before we can define high-order Nedelec
   //    spaces on them.
   ParMesh *pmesh = NULL;

   const bool geometricPartition = true;

   if (geometricPartition)
     {
       //int nxyzGlobal[3] = {1, 1, 1};
       //int nxyzGlobal[3] = {1, 1, 2};
       //int nxyzGlobal[3] = {1, 2, 1};
       //int nxyzGlobal[3] = {2, 1, 2};
       int nxyzGlobal[3] = {2, 2, 4};
       //int nxyzGlobal[3] = {4, 4, 4};
       //int nxyzGlobal[3] = {2, 2, 8};
       //int nxyzGlobal[3] = {6, 6, 8};  // 288
       //int nxyzGlobal[3] = {6, 6, 16};  // 576
       //int nxyzGlobal[3] = {6, 6, 32};  // 1152
       //int nxyzGlobal[3] = {8, 4, 8};
       //int nxyzGlobal[3] = {8, 16, 8};
       int *partition = mesh->CartesianPartitioning(nxyzGlobal);
       
       pmesh = new ParMesh(MPI_COMM_WORLD, *mesh, partition);

       // pmesh = new ParMesh(MPI_COMM_WORLD, *mesh);

       if (myid == 0)
	 {
	   cout << "Parallel partition " << nxyzGlobal[0] << ", " << nxyzGlobal[1] << ", " << nxyzGlobal[2] << endl;
	 }

       /*
       std::vector<int> partition;
       partition.assign(mesh->GetNE(), -1);

       Vector ec;
       
       for (int i=0; i<mesh->GetNE(); ++i)
	 {
	   mesh->GetElementCenter(i, ec);
	   partition[i] = 0;
	 }
       
       pmesh = new ParMesh(MPI_COMM_WORLD, *mesh, partition.data());
       */
     }
   else
     {
       pmesh = new ParMesh(MPI_COMM_WORLD, *mesh);
     }
   
   delete mesh;
   {
      int par_ref_levels = 1;
      for (int l = 0; l < par_ref_levels; l++)
      {
         pmesh->UniformRefinement();
      }
   }
   pmesh->ReorientTetMesh();

   double hmin = 0.0;   
   {
     double minsize = pmesh->GetElementSize(0);
     double maxsize = minsize;
     for (int i=1; i<pmesh->GetNE(); ++i)
       {
	 const double size_i = pmesh->GetElementSize(i);
	 minsize = std::min(minsize, size_i);
	 maxsize = std::max(maxsize, size_i);
       }

     cout << myid << ": Element size range: (" << minsize << ", " << maxsize << ")" << endl;

     MPI_Allreduce(&minsize, &hmin, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
   }

   // 5.1. Determine subdomain interfaces, and for each interface create a set of local vertex indices in pmesh.
   SubdomainInterfaceGenerator sdInterfaceGen(numSubdomains, pmesh);
   vector<SubdomainInterface> interfaces;  // Local interfaces
   sdInterfaceGen.CreateInterfaces(interfaces);

   /*
   { // Debugging
     for (std::set<int>::const_iterator it = interfaces[0].faces.begin(); it != interfaces[0].faces.end(); ++it)
       {
	 cout << myid << ": iface " << *it << endl;
       }
   }
   */
   
   std::vector<int> interfaceGlobalToLocalMap, interfaceGI;
   const int numInterfaces = sdInterfaceGen.GlobalToLocalInterfaceMap(interfaces, interfaceGlobalToLocalMap, interfaceGI);
   
   cout << myid << ": created " << numSubdomains << " subdomains with " << numInterfaces <<  " interfaces" << endl;
   
   // 5.2. Create parallel subdomain meshes.
   SubdomainParMeshGenerator sdMeshGen(numSubdomains, pmesh);
   ParMesh **pmeshSD = sdMeshGen.CreateParallelSubdomainMeshes();

   if (pmeshSD == NULL)
     return 2;

   // 5.3. Create parallel interface meshes.
   ParMesh **pmeshInterfaces = (numInterfaces > 0 ) ? new ParMesh*[numInterfaces] : NULL;

   for (int i=0; i<numInterfaces; ++i)
     {
       const int iloc = interfaceGlobalToLocalMap[i];  // Local interface index
       if (iloc >= 0)
	 {
	   MFEM_VERIFY(interfaceGI[i] == interfaces[iloc].GetGlobalIndex(), "");
	   pmeshInterfaces[i] = sdMeshGen.CreateParallelInterfaceMesh(interfaces[iloc]);
	 }
       else
	 {
	   // This is not elegant. 
	   const int sd0 = interfaceGI[i] / numSubdomains;  // globalIndex = (numSubdomains * sd0) + sd1;
	   const int sd1 = interfaceGI[i] - (numSubdomains * sd0);  // globalIndex = (numSubdomains * sd0) + sd1;
	   SubdomainInterface emptyInterface(sd0, sd1);
	   emptyInterface.SetGlobalIndex(numSubdomains);
	   pmeshInterfaces[i] = sdMeshGen.CreateParallelInterfaceMesh(emptyInterface);
	 }
     }

   /*
   {
     // Print the first vertex on each process, in order of rank.
     // This shows that as rank increases, the x-coordinate increases fastest, z slowest.
     // Consequently, partitioning the subdomains in the z direction will ensure that 
     // different subdomains are on different nodes, so that matrix memory is distributed.

     if (myid > 0)
       {
	 MPI_Status stat;
	 int num;
	 MPI_Recv(&num, 1, MPI_INT, myid-1, myid-1, MPI_COMM_WORLD, &stat);
       }
     
     cout << myid << ": first vertex " << pmesh->GetVertex(0)[0] << " " << pmesh->GetVertex(0)[1] << " "  << pmesh->GetVertex(0)[2] << endl;

     if (myid < num_procs-1)
       {
	 MPI_Send(&myid, 1, MPI_INT, myid+1, myid, MPI_COMM_WORLD);
       }
     
     return;
   }
   */
   /*
   { // debugging
     for (int i=0; i<numSubdomains; ++i)
       {
	 cout << "SD " << i << " y coordinate for vertex 0 " << pmeshSD[i]->GetVertex(0)[1] << endl;
       }

     for (int i=0; i<numInterfaces; ++i)
       {
	 cout << "IF " << i << " y coordinate for vertex 0 " << pmeshInterfaces[i]->GetVertex(0)[1] << endl;
       }
   }
   */
   
   // Note that subdomains do not overlap element-wise, and the parallel mesh of an individual subdomain has no element overlap on different processes.
   // However, the parallel mesh of an individual interface may have element (face) overlap on different processes, for the purpose of communication.
   // It is even possible (if an interface lies on a process boundary) for an entire interface to be duplicated on two processes, with zero true DOF's
   // on one process. 
   
   const bool testSubdomains = false;
   if (testSubdomains)
     {
       for (int i=0; i<numSubdomains; ++i)
	 {
	   ostringstream filename;
	   filename << "sd" << setfill('0') << setw(3) << i;
	   VisitTestPlotParMesh(filename.str(), pmeshSD[i], -1, myid);
	   //PrintMeshBoundingBox2(pmeshSD[i]);
	 }
       
       for (int i=0; i<numInterfaces; ++i)
	 {
	   ostringstream filename;
	   filename << "sdif" << setfill('0') << setw(3) << i;
	   VisitTestPlotParMesh(filename.str(), pmeshInterfaces[i], i, myid);
	 }
       
       const bool printInterfaceVertices = false;
       if (printInterfaceVertices)
	 {
	   for (int i=0; i<interfaces.size(); ++i)
	     {
	       cout << myid << ": Interface " << interfaces[i].GetGlobalIndex() << " has " << interfaces[i].NumVertices() << endl;
	       interfaces[i].PrintVertices(pmesh);
	     }
	 }
     }

   //TestHypreIdentity(MPI_COMM_WORLD);
   //TestHypreRectangularSerial();

   // 6. Define a parallel finite element space on the parallel mesh. Here we
   //    use the Nedelec finite elements of the specified order.
   FiniteElementCollection *fec = new ND_FECollection(order, dim);
   ParFiniteElementSpace *fespace = new ParFiniteElementSpace(pmesh, fec);
   HYPRE_Int size = fespace->GlobalTrueVSize();
   long globalNE = pmesh->GetGlobalNE();
   if (myid == 0)
   {
     cout << "Number of mesh elements: " << globalNE << endl;
     cout << "Number of finite element unknowns: " << size << endl;
     cout << "Root local number of finite element unknowns: " << fespace->TrueVSize() << endl;
   }

   /*
   if (myid == 0)
   { // Print the interface mesh
     ostringstream mesh_name;
     mesh_name << "ifmesh." << setfill('0') << setw(6) << myid;

     ofstream mesh_ofs(mesh_name.str().c_str());
     mesh_ofs.precision(8);
     pmeshInterfaces[0]->Print(mesh_ofs);
   }
   */
   
   // 6.1. Create interface operator.

   DDMInterfaceOperator ddi(numSubdomains, numInterfaces, pmesh, fespace, pmeshSD, pmeshInterfaces, order, pmesh->Dimension(),
			    &interfaces, &interfaceGlobalToLocalMap, -SIGMAVAL, hmin);  // PengLee2012 uses order 2 

   cout << "DDI size " << ddi.Height() << " by " << ddi.Width() << endl;
   
   //return 0;
   
   //PrintDenseMatrixOfOperator(ddi, num_procs, myid);
   
   // 7. Determine the list of true (i.e. parallel conforming) essential
   //    boundary dofs. In this example, the boundary conditions are defined
   //    by marking all the boundary attributes from the mesh as essential
   //    (Dirichlet) and converting them to a list of true dofs.
   Array<int> ess_tdof_list;
   Array<int> ess_bdr;
   if (pmesh->bdr_attributes.Size())
   {
     //Array<int> ess_bdr(pmesh->bdr_attributes.Max());
      ess_bdr.SetSize(pmesh->bdr_attributes.Max());
      ess_bdr = 1;
      fespace->GetEssentialTrueDofs(ess_bdr, ess_tdof_list);
   }

   // 8. Set up the parallel linear form b(.) which corresponds to the
   //    right-hand side of the FEM linear system, which in this case is
   //    (f,phi_i) where f is given by the function f_exact and phi_i are the
   //    basis functions in the finite element fespace.
   //VectorFunctionCoefficient f(sdim, f_exact);
   VectorFunctionCoefficient f(sdim, test2_RHS_exact);
   ParLinearForm *b = new ParLinearForm(fespace);
   b->AddDomainIntegrator(new VectorFEDomainLFIntegrator(f));
   b->Assemble();

   // 9. Define the solution vector x as a parallel finite element grid function
   //    corresponding to fespace. Initialize x by projecting the exact
   //    solution. Note that only values from the boundary edges will be used
   //    when eliminating the non-homogeneous boundary condition to modify the
   //    r.h.s. vector b.
   ParGridFunction x(fespace);
   //VectorFunctionCoefficient E(sdim, E_exact);
   VectorFunctionCoefficient E(sdim, test2_E_exact);
   x.ProjectCoefficient(E);

   // 10. Set up the parallel bilinear form corresponding to the EM diffusion
   //     operator curl muinv curl + sigma I, by adding the curl-curl and the
   //     mass domain integrators.
   Coefficient *muinv = new ConstantCoefficient(1.0);
   Coefficient *sigma = new ConstantCoefficient(SIGMAVAL);
   //Coefficient *sigmaAbs = new ConstantCoefficient(fabs(SIGMAVAL));
   ParBilinearForm *a = new ParBilinearForm(fespace);
   a->AddDomainIntegrator(new CurlCurlIntegrator(*muinv));

#ifdef AIRY_TEST
   VectorFunctionCoefficient epsilon(3, test_Airy_epsilon);
   a->AddDomainIntegrator(new VectorFEMassIntegrator(epsilon));
#else
   a->AddDomainIntegrator(new VectorFEMassIntegrator(*sigma));
#endif
   
   //cout << myid << ": NBE " << pmesh->GetNBE() << endl;

   // 11. Assemble the parallel bilinear form and the corresponding linear
   //     system, applying any necessary transformations such as: parallel
   //     assembly, eliminating boundary conditions, applying conforming
   //     constraints for non-conforming AMR, static condensation, etc.
   if (static_cond) { a->EnableStaticCondensation(); }
   a->Assemble();
   a->Finalize();

   /*
   Vector exactSol(fespace->GetTrueVSize());
   x.GetTrueDofs(exactSol);
   */
   
   HypreParMatrix A;
   Vector B, X;
   a->FormLinearSystem(ess_tdof_list, x, *b, A, X, B);
   //a->FormSystemMatrix(ess_tdof_list, A);

   /*
   {
     Vector Ax(fespace->GetTrueVSize());
     A.Mult(exactSol, Ax);

     Ax -= B;
     cout << "Global projected error " << Ax.Norml2() << " relative to " << B.Norml2() << endl;
   }
   */
   
   //HypreParMatrix *Apa = a->ParallelAssemble();
   DenseMatrixInverse dddinv;
   
   if (false)
   { // Output ddi as a DenseMatrix
     const int Ndd = ddi.Height();
     Vector ej(Ndd);
     Vector Aej(Ndd);
     DenseMatrix ddd(Ndd);

     ofstream sp("ddisparse.txt");

     const bool writeFile = true;
     
     for (int j=0; j<Ndd; ++j)
       {
	 cout << "Computing column " << j << " of " << Ndd << " of ddi" << endl;
	 
	 ej = 0.0;
	 ej[j] = 1.0;
	 ddi.Mult(ej, Aej);

	 for (int i=0; i<Ndd; ++i)
	   {
	     ddd(i,j) = Aej[i];
	     
	     if (writeFile)
	       {
		 if (fabs(Aej[i]) > 1.0e-15)
		   sp << i+1 << " " << j+1 << " " << Aej[i] << endl;
	       }
	   }
       }

     sp.close();

     /*
     cout << "Factoring dense matrix" << endl;
     dddinv.Factor(ddd);
     cout << "Done factoring dense matrix" << endl;

     if (writeFile)
       {
	 ofstream ost("ddimat5.mat", std::ofstream::out);
	 ddd.PrintMatlab(ost);
       }
     */
   }

   if (false)
   { // Test projection as solution
     ParBilinearForm *mbf = new ParBilinearForm(fespace);
     mbf->AddDomainIntegrator(new VectorFEMassIntegrator(*muinv));
     mbf->Assemble();
     mbf->Finalize();

     HypreParMatrix Mtest;

     mbf->FormSystemMatrix(ess_tdof_list, Mtest);
     //HypreParMatrix *Mpa = mbf->ParallelAssemble();
     
     ParGridFunction tgf(fespace);

     Vector uproj(fespace->GetTrueVSize());
     Vector Auproj(fespace->GetTrueVSize());
     Vector yproj(fespace->GetTrueVSize());
     Vector Myproj(fespace->GetTrueVSize());
     Vector MinvAuproj(fespace->GetTrueVSize());

     VectorFunctionCoefficient utest(3, test2_E_exact);
     VectorFunctionCoefficient ytest(3, test2_RHS_exact);

     tgf.ProjectCoefficient(utest);
     tgf.GetTrueDofs(uproj);

     tgf.ProjectCoefficient(ytest);
     tgf.GetTrueDofs(yproj);

     cout << myid << ": Norm of yproj " << yproj.Norml2() << endl;

     Mtest.Mult(yproj, Myproj);
     //Mpa->Mult(yproj, Myproj);

     cout << myid << ": Norm of Myproj " << Myproj.Norml2() << endl;

     A.Mult(uproj, Auproj);
     //Apa->Mult(uproj, Auproj);

     {
       HypreSolver *amg = new HypreBoomerAMG(Mtest);
       HyprePCG *pcg = new HyprePCG(Mtest);
       pcg->SetTol(1e-12);
       pcg->SetMaxIter(200);
       pcg->SetPrintLevel(2);
       pcg->SetPreconditioner(*amg);
       pcg->Mult(Auproj, MinvAuproj);

       //MinvAuproj -= yproj;
       
       tgf.SetFromTrueDofs(MinvAuproj);

       Vector zeroVec(3);
       zeroVec = 0.0;
       VectorConstantCoefficient vzero(zeroVec);

       //double L2e = tgf.ComputeL2Error(vzero);
       double L2e = tgf.ComputeL2Error(ytest);

       cout << myid << ": L2 error of MinvAuproj - yproj: " << L2e << endl;
     }
     
     cout << myid << ": Norm of Auproj " << Auproj.Norml2() << endl;

     Myproj -= Auproj;
     cout << myid << ": Norm of diff " << Myproj.Norml2() << endl;

     delete mbf;
   }

   //return;
   
   if (myid == 0)
   {
      cout << "Size of linear system: " << A.GetGlobalNumRows() << endl;
   }
   
   {
     cout << myid << ": A size " << A.Height() << " x " << A.Width() << endl;
     cout << myid << ": X size " << X.Size() << ", B size " << B.Size() << endl;
     cout << myid << ": fespace size " << fespace->GetVSize() << ", true size " << fespace->GetTrueVSize() << endl;
   }

   StopWatch chrono;
   chrono.Clear();
   chrono.Start();

   //TestStrumpackConstructor();

   const bool solveDD = true;
   if (solveDD)
     {
       cout << myid << ": B size " << B.Size() << ", norm " << B.Norml2() << endl;
       cout << myid << ": fespace true V size " << fespace->GetTrueVSize() << endl;

       Vector Bdd(ddi.Width());
       Vector xdd(ddi.Width());

       Vector B_Im(B.Size());
       B_Im = 0.0;
       //B_Im = B;
       //B = 0.0;
       
       ddi.GetReducedSource(fespace, B, B_Im, Bdd);

       //ddi.TestProjectionError();
       
       //ddi.FullSystemAnalyticTest();

       //Bdd = 1.0;

       /*
       if (myid == 0)
	 {
	   ofstream ddrhsfile("bddtmp");
	   Bdd.Print(ddrhsfile);
	 }
       if (myid == 1)
	 {
	   ofstream ddrhsfile("bddtmp1");
	   Bdd.Print(ddrhsfile);
	 }
       */

       cout << myid << ": Bdd norm " << Bdd.Norml2() << endl;

       cout << "Solving DD system with gmres" << endl;

       GMRESSolver *gmres = new GMRESSolver(fespace->GetComm());
       //MINRESSolver *gmres = new MINRESSolver(fespace->GetComm());
       //BiCGSTABSolver *gmres = new BiCGSTABSolver(fespace->GetComm());
       //OrthominSolver *gmres = new OrthominSolver(fespace->GetComm());
       
       gmres->SetOperator(ddi);
       gmres->SetRelTol(1e-8);
       gmres->SetMaxIter(100);
       gmres->SetKDim(100);
       gmres->SetPrintLevel(1);

       //gmres->SetName("ddi");

       StopWatch chronoSolver;
       chronoSolver.Clear();
       chronoSolver.Start();
       
       xdd = 0.0;
       gmres->Mult(Bdd, xdd);
       //ddi.Mult(Bdd, xdd);

       //dddinv.Mult(Bdd, xdd);
       /*
       {
	 cout << "Reading FEM solution from file" << endl;
	 ifstream ddsolfile("xfemsp");
	 //ifstream ddsolfile("xfemsp2sd");
	 for (int i=0; i<X.Size(); ++i)
	   ddsolfile >> X[i];
       }
       */
       /*       
       {
	 cout << "Reading solution from file" << endl;
	 //ifstream ddsolfile("xddd4sd");
	 ifstream ddsolfile("xgm4sd0rho");
	 for (int i=0; i<xdd.Size(); ++i)
	   ddsolfile >> xdd[i];
	 
	 //xdd.Load(ddsolfile);
       }
       */
       
       cout << myid << ": xdd norm " << xdd.Norml2() << endl;

       /*
       if (myid == 0)
       {
	 ofstream ddsolfile("xgm4sd");
	 xdd.Print(ddsolfile);
       }
       */

       Vector Xfem(X.Size());
       Xfem = X;
       X = 0.0;
       
       ddi.RecoverDomainSolution(fespace, xdd, Xfem, X);

       //ddi.TestReconstructedFullDDSolution();
       
       chronoSolver.Stop();
       if (myid == 0)
	 cout << myid << ": Solver and recovery only time " << chronoSolver.RealTime() << endl;

       delete gmres;
     }

#ifdef MFEM_USE_STRUMPACK
   if (use_strumpack)
     {
       const bool fullDirect = true;

       if (fullDirect)
	 {
	   /*
	   cout << "FULL DIRECT SOLVER" << endl;

	   Operator * Arow = new STRUMPACKRowLocMatrix(A);

	   STRUMPACKSolver * strumpack = new STRUMPACKSolver(argc, argv, MPI_COMM_WORLD);
	   strumpack->SetPrintFactorStatistics(true);
	   strumpack->SetPrintSolveStatistics(false);
	   strumpack->SetKrylovSolver(strumpack::KrylovSolver::DIRECT);
	   strumpack->SetReorderingStrategy(strumpack::ReorderingStrategy::METIS);
	   // strumpack->SetMC64Job(strumpack::MC64Job::NONE);
	   // strumpack->SetSymmetricPattern(true);
	   strumpack->SetOperator(*Arow);
	   strumpack->SetFromCommandLine();
	   //Solver * precond = strumpack;

	   strumpack->Mult(B, X);

	   if (myid == -10)
	     {
	       ofstream solfile("xairy27b");
	       X.Print(solfile);
	     }

	   {
	     // Check residual
	     Vector res(X.Size());
	     Vector ssol(X.Size());
	     ssol = X;

	     const double Bnrm = B.Norml2();
	     const double Bnrm2 = Bnrm*Bnrm;
	     
	     A.Mult(ssol, res);
	     res -= B;

	     const double Rnrm = res.Norml2();
	     const double Rnrm2 = Rnrm*Rnrm;

	     double sumBnrm2 = 0.0;
	     double sumRnrm2 = 0.0;
	     MPI_Allreduce(&Bnrm2, &sumBnrm2, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	     MPI_Allreduce(&Rnrm2, &sumRnrm2, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

	     if (myid == 0)
	       {
		 cout << myid << ": STRUMPACK residual norm " << sqrt(sumRnrm2) << ", B norm " << sqrt(sumBnrm2) << endl;
	       }
	   }
	   
	   delete strumpack;
	   delete Arow;
	   */
	 }
       else
	 {
	   ParFiniteElementSpace *prec_fespace =
	     (a->StaticCondensationIsEnabled() ? a->SCParFESpace() : fespace);
	   HypreSolver *ams = new HypreAMS(A, prec_fespace);

#ifdef HYPRE_DYLAN
	   {
	     Vector Xtmp(X);
	     ams->Mult(B, Xtmp);  // Just a hack to get ams to run its setup function. There should be a better way.
	   }

	   GMRESSolver *gmres = new GMRESSolver(fespace->GetComm());
	   //FGMRESSolver *gmres = new FGMRESSolver(fespace->GetComm());
	   //BiCGSTABSolver *gmres = new BiCGSTABSolver(fespace->GetComm());
	   //MINRESSolver *gmres = new MINRESSolver(fespace->GetComm());
	   
	   gmres->SetOperator(A);
	   gmres->SetRelTol(1e-16);
	   gmres->SetMaxIter(1000);
	   gmres->SetPrintLevel(1);

	   gmres->SetPreconditioner(*ams);
	   gmres->Mult(B, X);
#else
	   HypreGMRES *gmres = new HypreGMRES(A);
	   gmres->SetTol(1e-12);
	   gmres->SetMaxIter(100);
	   gmres->SetPrintLevel(10);
	   gmres->SetPreconditioner(*ams);
	   gmres->Mult(B, X);

	   delete gmres;
	   //delete iams;
	   //delete ams;
#endif
	 }
     }
   else
#endif
     {
       // 12. Define and apply a parallel PCG solver for AX=B with the AMS
       //     preconditioner from hypre.
       ParFiniteElementSpace *prec_fespace =
	 (a->StaticCondensationIsEnabled() ? a->SCParFESpace() : fespace);
       HypreSolver *ams = new HypreAMS(A, prec_fespace);
       HyprePCG *pcg = new HyprePCG(A);
       pcg->SetTol(1e-12);
       pcg->SetMaxIter(100);
       pcg->SetPrintLevel(2);
       pcg->SetPreconditioner(*ams);
       pcg->Mult(B, X);

       delete pcg;
       delete ams;
     }
   
   chrono.Stop();
   cout << myid << ": Total DDM time (setup, solver, recovery) " << chrono.RealTime() << endl;

   // 13. Recover the parallel grid function corresponding to X. This is the
   //     local finite element solution on each processor.
   a->RecoverFEMSolution(X, *b, x);

   // 14. Compute and print the L^2 norm of the error.
   {
      double err = x.ComputeL2Error(E);
      Vector zeroVec(3);
      zeroVec = 0.0;
      VectorConstantCoefficient vzero(zeroVec);
      ParGridFunction zerogf(fespace);
      zerogf = 0.0;
      double normE = zerogf.ComputeL2Error(E);
      double normX = x.ComputeL2Error(vzero);
      if (myid == 0)
      {
         cout << "|| E_h - E ||_{L^2} = " << err << endl;
	 cout << "|| E_h ||_{L^2} = " << normX << endl;
	 cout << "|| E ||_{L^2} = " << normE << endl;
      }
   }

   // 15. Save the refined mesh and the solution in parallel. This output can
   //     be viewed later using GLVis: "glvis -np <np> -m mesh -g sol".
   /*
   {
      ostringstream mesh_name, sol_name;
      mesh_name << "mesh." << setfill('0') << setw(6) << myid;
      sol_name << "sol." << setfill('0') << setw(6) << myid;

      ofstream mesh_ofs(mesh_name.str().c_str());
      mesh_ofs.precision(8);
      pmesh->Print(mesh_ofs);

      ofstream sol_ofs(sol_name.str().c_str());
      sol_ofs.precision(8);
      x.Save(sol_ofs);
   }
   */
   
   // 16. Send the solution by socket to a GLVis server.
   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock(vishost, visport);
      sol_sock << "parallel " << num_procs << " " << myid << "\n";
      sol_sock.precision(8);
      sol_sock << "solution\n" << *pmesh << x << flush;
   }
  
   //cout << "Final element 0 size " << pmesh->GetElementSize(0) << ", number of elements " << pmesh->GetGlobalNE() << endl;
   
   // Create data collection for solution output: either VisItDataCollection for
   // ascii data files, or SidreDataCollection for binary data files.
   DataCollection *dc = NULL;
   if (visit)
     {
       bool binary = false;
       if (binary)
	 {
#ifdef MFEM_USE_SIDRE
	   dc = new SidreDataCollection("ddsol", pmesh);
#else
	   MFEM_ABORT("Must build with MFEM_USE_SIDRE=YES for binary output.");
#endif
	 }
       else
	 {
	   dc = new VisItDataCollection("ddsol", pmesh);
	   dc->SetPrecision(8);
	   // To save the mesh using MFEM's parallel mesh format:
	   // dc->SetFormat(DataCollection::PARALLEL_FORMAT);
	 }
       dc->RegisterField("solution", &x);
       dc->SetCycle(0);
       dc->SetTime(0.0);
       dc->Save();
     }
   
   // 17. Free the used memory.
   delete a;
   delete sigma;
   delete muinv;
   delete b;
   delete fespace;
   delete fec;
   delete pmesh;
   
   MPI_Finalize();

   return 0;
}


void E_exact(const Vector &x, Vector &E)
{
   if (dim == 3)
   {
      E(0) = sin(kappa * x(1));
      E(1) = sin(kappa * x(2));
      E(2) = sin(kappa * x(0));
   }
   else
   {
      E(0) = sin(kappa * x(1));
      E(1) = sin(kappa * x(0));
      if (x.Size() == 3) { E(2) = 0.0; }
   }
}

void f_exact(const Vector &x, Vector &f)
{
   if (dim == 3)
   {
      f(0) = (SIGMAVAL + kappa * kappa) * sin(kappa * x(1));
      f(1) = (SIGMAVAL + kappa * kappa) * sin(kappa * x(2));
      f(2) = (SIGMAVAL + kappa * kappa) * sin(kappa * x(0));
   }
   else
   {
      f(0) = (1. + kappa * kappa) * sin(kappa * x(1));
      f(1) = (1. + kappa * kappa) * sin(kappa * x(0));
      if (x.Size() == 3) { f(2) = 0.0; }
   }
}

double radiusFunction(const Vector &x)
{
  double f = 0.0;
  for (int i=0; i<dim; ++i)
    f += x[i]*x[i];
  
  return sqrt(f);
}
