﻿//            MFEM Shifted boundary method solver - Parallel Version
//
// Compile with: make diffusion
//
// Sample runs:
//   Problem 1: Circular hole of radius 0.2 at the center of the domain.
//              -nabla^u = 1 with homogeneous boundary conditions
//make neumann;mpirun -np 1 neumann -m ../../data/inline-quad.mesh -rs 3 -o 1 -vis -lst 1

#include "../../mfem.hpp"
#include <fstream>
#include <iostream>
#include "sbm-aux.hpp"

using namespace mfem;
using namespace std;

void normal_vector(const Vector &x, Vector &p) {
    p.SetSize(x.Size());
    p(0) = x(0)-0.5;
    p(1) = x(1)-0.5; //center of circle at [0.5, 0.5]
    p /= p.Norml2();
    p *= -1;
}

int main(int argc, char *argv[])
{
   // Initialize MPI.
   int num_procs, myid;
   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
   MPI_Comm_rank(MPI_COMM_WORLD, &myid);

   // Parse command-line options.
   const char *mesh_file = "../../data/inline-quad.mesh";
   int order = 2;
   bool pa = false;
   const char *device_config = "cpu";
   bool visualization = true;
   int ser_ref_levels = 0;
   //bool exact = true;
   int level_set_type = 1;
   int ho_terms = 1;
   double alpha = 1;
   bool trimin = true;

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree) or -1 for"
                  " isoparametric space.");
   args.AddOption(&pa, "-pa", "--partial-assembly", "-no-pa",
                  "--no-partial-assembly", "Enable Partial Assembly.");
   args.AddOption(&device_config, "-d", "--device",
                  "Device configuration string, see Device::Configure().");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&ser_ref_levels, "-rs", "--refine-serial",
                  "Number of times to refine the mesh uniformly in serial.");
//   args.AddOption(&exact, "-ex", "--exact", "-no-ex",
//                  "--no-exact",
//                  "Use exact representaion of distance vector function.");
   args.AddOption(&level_set_type, "-lst", "--level-set-type",
                  "level-set-type:");
   args.AddOption(&ho_terms, "-ho", "--high-order",
                  "Additional high-order terms to include");
   args.AddOption(&alpha, "-alpha", "--alpha",
                  "Nitsche penalty parameter (~1 for 2D, ~10 for 3D).");
   args.AddOption(&trimin, "-trim", "--trim", "-out-trim",
                  "--out-trim",
                  "Trim inside or outside.");

   args.Parse();
   if (!args.Good())
   {
      args.PrintUsage(cout);
      return 1;
   }
   args.PrintOptions(cout);

   // Enable hardware devices such as GPUs, and programming models such as
   // CUDA, OCCA, RAJA and OpenMP based on command line options.
   Device device(device_config);
   device.Print();

   // Read the mesh from the given mesh file. We can handle triangular,
   // quadrilateral, tetrahedral, hexahedral, surface and volume meshes with
   // the same code.
   Mesh mesh(mesh_file, 1, 1);
   int dim = mesh.Dimension();
   for (int lev = 0; lev < ser_ref_levels; lev++) { mesh.UniformRefinement(); }

   ParMesh pmesh(MPI_COMM_WORLD, mesh);
   mesh.Clear();
   {
      int par_ref_levels = 0;
      for (int l = 0; l < par_ref_levels; l++)
      {
         pmesh.UniformRefinement();
      }
   }

   // Define a finite element space on the mesh. Here we use continuous
   // Lagrange finite elements of the specified order. If order < 1, we
   // instead use an isoparametric/isogeometric space.
   FiniteElementCollection *fec;
   if (order > 0)
   {
      fec = new H1_FECollection(order, dim);
   }
   else
   {
      fec = new H1_FECollection(order = 1, dim);
   }
   ParFiniteElementSpace pfespace(&pmesh, fec);

   Vector vxyz;

   ParFiniteElementSpace pfespace_mesh(&pmesh, fec, dim);
   pmesh.SetNodalFESpace(&pfespace_mesh);
   ParGridFunction x_mesh(&pfespace_mesh);
   pmesh.SetNodalGridFunction(&x_mesh);
   vxyz = *pmesh.GetNodes();
   int nodes_cnt = vxyz.Size()/dim;
   if (level_set_type == 3) { //stretch quadmesh from [0, 1] to [-1.e-4, 1]
       for (int i = 0; i < nodes_cnt; i++) {
           vxyz(i+nodes_cnt) = (1.+1.e-4)*vxyz(i+nodes_cnt)-1.e-4;
       }
   }
   pmesh.SetNodes(vxyz);
   pfespace.ExchangeFaceNbrData();
   cout << "Number of finite element unknowns: "
        << pfespace.GetTrueVSize() << endl;

   // Define the solution vector x as a finite element grid function
   // corresponding to fespace. Initialize x with initial guess of zero,
   // which satisfies the boundary conditions.
   ParGridFunction x(&pfespace);
   // ParGridFunction for level_set_value.
   ParGridFunction level_set_val(&pfespace);

   // Determine if each element in the ParMesh is inside the actual domain,
   // partially cut by the actual domain boundary, or completely outside
   // the domain.
   Dist_Level_Set_Coefficient dist_fun_level_coef(level_set_type);
   level_set_val.ProjectCoefficient(dist_fun_level_coef);
   level_set_val.ExchangeFaceNbrData();

   IntegrationRules IntRulesLo(0, Quadrature1D::GaussLobatto);
   FaceElementTransformations *tr = NULL;

   // Set elem_marker based on the distance field:
   // 0 if completely in the domain
   // 1 if completely outside the domain
   // 2 if partially inside the domain
   Array<int> elem_marker(pmesh.GetNE()+pmesh.GetNSharedFaces());
   elem_marker = 0;
   Vector vals;
   // Check elements on the current MPI rank
   for (int i = 0; i < pmesh.GetNE(); i++)
   {
      ElementTransformation *Tr = pmesh.GetElementTransformation(i);
      const IntegrationRule &ir =
         IntRulesLo.Get(pmesh.GetElementBaseGeometry(i), 4*Tr->OrderJ());
      level_set_val.GetValues(i, ir, vals);

      pmesh.SetAttribute(i, 1);

      int count = 0;
      for (int j = 0; j < ir.GetNPoints(); j++)
      {
         double val = vals(j);
         if (val <= 0.) { count++; }
      }
      if (count == ir.GetNPoints()) // completely outside
      {
         elem_marker[i] = 1;
         pmesh.SetAttribute(i, 2);
      }
      else if (count > 0) // partially outside
      {
         elem_marker[i] = 2;
         int attr = trimin ? 2 : 1;
         pmesh.SetAttribute(i, attr);
      }
   }

   // Check neighbors on the adjacent MPI rank
   for (int i = pmesh.GetNE(); i < pmesh.GetNE()+pmesh.GetNSharedFaces(); i++)
   {
       int shared_fnum = i-pmesh.GetNE();
       tr = pmesh.GetSharedFaceTransformations(shared_fnum);
       int Elem2NbrNo = tr->Elem2No - pmesh.GetNE();

       ElementTransformation *eltr =
               pfespace.GetFaceNbrElementTransformation(Elem2NbrNo);
       const IntegrationRule &ir =
         IntRulesLo.Get(pfespace.GetFaceNbrFE(Elem2NbrNo)->GetGeomType(),
                        4*eltr->OrderJ());

       const int nip = ir.GetNPoints();
       vals.SetSize(nip);
       int count = 0;
       for (int j = 0; j < nip; j++) {
          const IntegrationPoint &ip = ir.IntPoint(j);
          vals[j] = level_set_val.GetValue(tr->Elem2No, ip);
          if (vals[j] <= 0.) { count++; }
       }

      if (count == ir.GetNPoints()) // completely outside
      {
         elem_marker[i] = 1;
      }
      else if (count > 0) // partially outside
      {
         elem_marker[i] = 2;
      }
   }

   // Setup a gridfunction with the element markers and output
   L2_FECollection fecl2 = L2_FECollection(0, dim);
   ParFiniteElementSpace pfesl2(&pmesh, &fecl2);
   ParGridFunction elem_marker_gf(&pfesl2);
   for (int i = 0; i < elem_marker_gf.Size(); i++)
   {
      elem_marker_gf(i) = elem_marker[i]*1.;
   }

   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock(vishost, visport);
      sol_sock.precision(8);
      sol_sock << "parallel " << num_procs << " " << myid << "\n";
      sol_sock << "solution\n" << pmesh << elem_marker_gf << flush;
      sol_sock << "window_title 'Element flags'\n"
               << "window_geometry "
               << 0 << " " << 0 << " " << 350 << " " << 350 << "\n"
               << "keys Rjmpc" << endl;
   }

   // Get a list of dofs associated with shifted boundary faces.
   Array<int> sbm_dofs; // Array of dofs on SBM faces
   Array<int> dofs;     // work array

   // First we check interior faces of the mesh (excluding interior faces that
   // are on the processor boundaries)
   double count1 = 0;
   for (int i = 0; i < pmesh.GetNumFaces(); i++)
   {
      FaceElementTransformations *tr = NULL;
      tr = pmesh.GetInteriorFaceTransformations (i);
      const int faceno = i;
      if (tr != NULL)
      {
         count1 += 1;
         int ne1 = tr->Elem1No;
         int ne2 = tr->Elem2No;
         int te1 = elem_marker[ne1], te2 = elem_marker[ne2];
         int check_val = 0;
         if (!trimin) { check_val = 1; }
         if (te1 == 2 && te2 == check_val)
         {
             pfespace.GetFaceDofs(faceno, dofs);
             sbm_dofs.Append(dofs);
         }
         if (te1 == check_val && te2 == 2)
         {
             pfespace.GetFaceDofs(faceno, dofs);
             sbm_dofs.Append(dofs);
         }
      }
   }

   // Here we add boundary faces that we want to model as SBM faces.
   // For the method where we clip inside the domain, a boundary face
   // has to be set as SBM face using its attribute.
   double count2 = 0;
   for (int i = 0; i < pmesh.GetNBE(); i++)
   {
      int attr = pmesh.GetBdrAttribute(i);
      FaceElementTransformations *tr;
      tr = pmesh.GetBdrFaceTransformations (i);
      if (tr != NULL) {
          int ne1 = tr->Elem1No;
          int te1 = elem_marker[ne1];
          if (attr == 100) { // add all boundary faces with attr=100 as SBM faces
              count2 += 1;
              const int faceno = pmesh.GetBdrFace(i);
              if (te1 == 0)
              {
                 pfespace.GetFaceDofs(faceno, dofs);
                 sbm_dofs.Append(dofs);
              }
          }
          else if (!trimin && te1 == 1) {
              // add all boundary faces if we are trimming out
              // and element is partially cut
              count2 += 1;
              const int faceno = pmesh.GetBdrFace(i);
              pfespace.GetFaceDofs(faceno, dofs);
              sbm_dofs.Append(dofs);
          }
      }
   }

   // Now we add interior faces that are on processor boundaries.
   double count3 = 0;
   double count3b = 0;
   for (int i = 0; i < pmesh.GetNSharedFaces(); i++)
   {
      tr = pmesh.GetSharedFaceTransformations(i);
      if (tr != NULL)
      {
         count3b += 1;
         int ne1 = tr->Elem1No;
         int te1 = elem_marker[ne1];
         int te2 = elem_marker[i+pmesh.GetNE()];
         const int faceno = pmesh.GetSharedFace(i);
         // Add if the element on this proc is completely inside the domain
         // and the the element on other proc is not
         int check_val = 0;
         if (!trimin) { check_val = 1; }
         if (te2 == 2 && te1 == check_val)
         {
            count3 += 1;
            pfespace.GetFaceDofs(faceno, dofs);
            sbm_dofs.Append(dofs);
         }
      }
   }


   // Determine the list of true (i.e. conforming) essential boundary dofs.
   // To do this, we first make a list of all dofs that are on the real boundary
   // of the mesh, then add all the dofs of the elements that are completely
   // outside or intersect shifted boundary. Then we remove the dofs from
   // SBM faces.

   // Make a list of dofs on all boundaries
   Array<int> ess_tdof_list;
   Array<int> ess_bdr(pmesh.bdr_attributes.Max());
   if (pmesh.bdr_attributes.Size())
   {
      ess_bdr = 1;
   }
   Array<int> ess_vdofs_bdr;
   pfespace.GetEssentialVDofs(ess_bdr, ess_vdofs_bdr);

   // Get all dofs associated with elements outside the domain or intersected
   // by the boundary.
   Array<int> ess_vdofs_hole(ess_vdofs_bdr.Size());
   ess_vdofs_hole = 0;
   int check_trim_flag = 0;
   if (!trimin) { check_trim_flag = 1; }
   for (int e = 0; e < pmesh.GetNE(); e++)
   {
      if ( (trimin && elem_marker[e] > 0) ||
         (!trimin && elem_marker[e] == 1) )
      {
         pfespace.GetElementVDofs(e, dofs);
         for (int i = 0; i < dofs.Size(); i++) {
             ess_vdofs_hole[dofs[i]] = -1;
         }
      }
   }

   // Combine the lists to mark essential dofs.
   for (int i = 0; i < ess_vdofs_hole.Size(); i++)
   {
      if (ess_vdofs_bdr[i] == -1) { ess_vdofs_hole[i] = -1; }
   }

   // Unmark dofs that are on SBM faces (but not on dirichlet boundaries)
   for (int i = 0; i < sbm_dofs.Size(); i++) {
       if (ess_vdofs_bdr[sbm_dofs[i]] != -1) { 
          ess_vdofs_hole[sbm_dofs[i]] = 0;
       }
   }

   // Synchronize
   for (int i = 0; i < ess_vdofs_hole.Size() ; i++) {
       ess_vdofs_hole[i] += 1;
   }

   pfespace.Synchronize(ess_vdofs_hole);

   for (int i = 0; i < ess_vdofs_hole.Size() ; i++) {
       ess_vdofs_hole[i] -= 1;
   }

   // convert to tdofs
   Array<int> ess_tdofs;
   pfespace.GetRestrictionMatrix()->BooleanMult(ess_vdofs_hole,
                                                ess_tdofs);
   pfespace.MarkerToList(ess_tdofs, ess_tdof_list);


   // Compute Distance Vector - Use analytic distance vectors for now.
   auto distance_vec_space = new ParFiniteElementSpace(pfespace.GetParMesh(),
                                                       pfespace.FEColl(), dim);
   ParGridFunction distance(distance_vec_space);

   // Get the Distance from the level set either using a numerical approach
   // or project an exact analytic function.
//   HeatDistanceSolver dist_func(1.0);
//   dist_func.smooth_steps = 1;
//   dist_func.ComputeVectorDistance(dist_fun_level_coef, distance);

   VectorCoefficient *dist_vec = NULL;
   if (true) {
       Dist_Vector_Coefficient *dist_vec_fcoeff =
               new Dist_Vector_Coefficient(dim, level_set_type);
       dist_vec = dist_vec_fcoeff;
       distance.ProjectDiscCoefficient(*dist_vec);
   }

   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock(vishost, visport);
      sol_sock.precision(8);
      sol_sock << "parallel " << num_procs << " " << myid << "\n";
      sol_sock << "solution\n" << pmesh << distance << flush;
      sol_sock << "window_title 'Distance Vector'\n"
               << "window_geometry "
               << 350 << " " << 350 << " " << 350 << " " << 350 << "\n"
               << "keys Rjmpcvv" << endl;
   }

   // Set up a list to indicate element attributes to be included in assembly.
   int max_elem_attr = pmesh.attributes.Max();
   Array<int> ess_elem(max_elem_attr);
   ess_elem = 1;
   ess_elem.Append(0);
   for (int i = 0; i < pmesh.GetNE(); i++) {
       if ( (trimin && elem_marker[i] > 0) ||
            (!trimin && elem_marker[i] == 1) )
       {
           pmesh.SetAttribute(i, max_elem_attr+1);
       }
   }
   pmesh.SetAttributes();

   // Set up the linear form b(.) which corresponds to the right-hand side of
   // the FEM linear system.
   ParLinearForm b(&pfespace);
   FunctionCoefficient *rhs_f = NULL;
   if (level_set_type == 1) {
       rhs_f = new FunctionCoefficient(rhs_fun_circle);
   }
   else if (level_set_type == 2) {
       rhs_f = new FunctionCoefficient(rhs_fun_xy_exponent);
   }
   else if (level_set_type == 3) {
       rhs_f = new FunctionCoefficient(rhs_fun_xy_sinusoidal);
   }
   else {
       MFEM_ABORT("Dirichlet velocity function not set for level set type.\n");
   }
   b.AddDomainIntegrator(new DomainLFIntegrator(*rhs_f), ess_elem);

   ShiftedFunctionCoefficient *dbcCoef = NULL;
   if (level_set_type == 1) {
       dbcCoef = new ShiftedFunctionCoefficient(dirichlet_velocity_circle);
   }
   else if (level_set_type == 2) {
       dbcCoef = new ShiftedFunctionCoefficient(dirichlet_velocity_xy_exponent);
   }
   else if (level_set_type == 3) {
       dbcCoef = new ShiftedFunctionCoefficient(dirichlet_velocity_xy_sinusoidal);
   }
   else {
       MFEM_ABORT("Dirichlet velocity function not set for level set type.\n");
   }

   ShiftedFunctionCoefficient *nbcCoef = NULL;
   ShiftedVectorFunctionCoefficient *normalbcCoef = NULL;
   if (level_set_type == 1) {
       nbcCoef = new ShiftedFunctionCoefficient(neumann_velocity_circle);
       normalbcCoef = new ShiftedVectorFunctionCoefficient(dim, normal_vector);
   }

   b.AddShiftedBdrFaceIntegrator(new SBM2NeumannLFIntegrator(
    *nbcCoef, alpha, *dist_vec, *normalbcCoef, ho_terms, trimin), elem_marker);
   b.Assemble();

   // Set up the bilinear form a(.,.) on the finite element space
   // corresponding to the Laplacian operator -Delta, by adding the Diffusion
   // domain integrator and SBM integrator.
   ParBilinearForm a(&pfespace);
   ConstantCoefficient one(1.);

   a.AddDomainIntegrator(new DiffusionIntegrator(one), ess_elem);
   a.AddShiftedBdrFaceIntegrator(new SBM2NeumannIntegrator(
   alpha, *dist_vec, *normalbcCoef, ho_terms, trimin), elem_marker);

   // Assemble the bilinear form and the corresponding linear system,
   // applying any necessary transformations.
   a.Assemble();

   // Project the exact solution as an initial condition for dirichlet boundaries.
   x = 0;
   //x.ProjectCoefficient(*dbcCoef);
   // Zero out non-essential boundaries.
   for (int i = 0; i < ess_vdofs_hole.Size(); i++) {
       if (ess_vdofs_hole[i] != -1) { x(i) = 0.; }
   }

   // Form the linear system and solve it.
   OperatorPtr A;
   Vector B, X;
   a.FormLinearSystem(ess_tdof_list, x, b, A, X, B);

   cout << "Size of linear system: " << A->Height() << endl;

   Solver *S = NULL;
   Solver *prec = NULL;
   prec = new HypreBoomerAMG;
   dynamic_cast<HypreBoomerAMG *>(prec)->SetPrintLevel(-1);
   //GMRESSolver *bicg = new GMRESSolver(MPI_COMM_WORLD);
   BiCGSTABSolver *bicg = new BiCGSTABSolver(MPI_COMM_WORLD);
   //CGSolver *bicg = new CGSolver(MPI_COMM_WORLD);
   bicg->SetRelTol(1e-12);
   bicg->SetMaxIter(2000);
   bicg->SetPrintLevel(1);
   bicg->SetPreconditioner(*prec);
   bicg->SetOperator(*A);
   S = bicg;
   S->Mult(B, X);

   // Recover the solution as a finite element grid function.
   a.RecoverFEMSolution(X, b, x);

   // Save the refined mesh and the solution. This output can be viewed later
   // using GLVis: "glvis -m refined.mesh -g sol.gf".
   ofstream mesh_ofs("ex1-sbm.mesh");
   mesh_ofs.precision(8);
   pmesh.PrintAsOne(mesh_ofs);
   ofstream sol_ofs("ex1-sbm.gf");
   sol_ofs.precision(8);
   x.SaveAsOne(sol_ofs);

   // Send the solution by socket to a GLVis server.
   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock(vishost, visport);
      sol_sock << "parallel " << num_procs << " " << myid << "\n";
      sol_sock << "solution\n" << pmesh << x << flush;
      sol_sock << "window_title 'Solution'\n"
               << "window_geometry "
               << 350 << " " << 0 << " " << 350 << " " << 350 << "\n"
               << "keys Rj" << endl;
   }

   // Construct an error gridfunction if the exact solution is known.
   ParGridFunction err(x);

   // use gslib to get diff from exact solution
   // please run ex1_exact_neumann.cpp in the shifted miniapps folder to generate these files
   Mesh mesh_comp("ex1n.mesh", 1, 1, false);
   ifstream mat_stream_1("ex1n.gf");
   GridFunction gf_comp(&mesh_comp, mat_stream_1);

   vxyz = *pmesh.GetNodes();

   Vector interp_vals(vxyz.Size());
   FindPointsGSLIB finder;
   finder.Setup(mesh_comp);
   finder.Interpolate(vxyz, gf_comp, interp_vals);

   for (int i = 0; i < nodes_cnt; i++) {
       err(i) = std::fabs(x(i) - interp_vals(i));
   }

   for (int i = 0; i < pmesh.GetNE(); i++) {
       Array<int> dofs;
       if (pmesh.GetAttribute(i) > max_elem_attr) {
           pfespace.GetElementDofs(i, dofs);
           for (int j = 0; j < dofs.Size(); j++) {
               if (ess_vdofs_hole[dofs[j]] == -1) {
                   err(dofs[j]) = 0.;
               }
           }
       }
   }

   double local_error = 0.;
   for (int i = 0; i < pfespace.GetNE(); i++) {
       if (pmesh.GetAttribute(i) > max_elem_attr) {
           continue;
       }
       const FiniteElement *fe = pfespace.GetFE(i);
       int intorder = 2*fe->GetOrder() + 3;
       const IntegrationRule *ir = &(IntRules.Get(fe->GetGeomType(), intorder));
       ElementTransformation *T = pfespace.GetElementTransformation(i);
       Vector loc_errs(ir->GetNPoints());
       err.GetValues(i, *ir, loc_errs);
       for (int j = 0; j < ir->GetNPoints(); j++)
       {
          const IntegrationPoint &ip = ir->IntPoint(j);
          T->SetIntPoint(&ip);
          local_error += ip.weight * T->Weight() * (loc_errs(j) * loc_errs(j));
       }
   }

   double global_error = local_error;
   MPI_Allreduce(&local_error, &global_error, 1, MPI_DOUBLE,
                 MPI_SUM, pfespace.GetComm());
   double global_error_inf = err.Normlinf();

   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock(vishost, visport);
      sol_sock.precision(8);
      sol_sock << "parallel " << num_procs << " " << myid << "\n";
      sol_sock << "solution\n" << pmesh << err << flush;
      sol_sock << "window_title 'Error'\n"
               << "window_geometry "
               << 700 << " " << 0 << " " << 350 << " " << 350 << "\n"
               << "keys Rj" << endl;

      osockstream sock(19916, "localhost");
      sock << "solution\n";
      mesh_comp.Print(sock);
      gf_comp.Save(sock);
      sock.send();
      sock << "window_title 'Exact'\n"
           << "window_geometry "
           << 1050 << " " << 0 << " " << 350 << " " << 350 << "\n"
           << "keys jRmclA" << endl;
   }


   int NEglob = pmesh.GetGlobalNE();
   if (level_set_type == 1 && myid == 0)
   {
      ofstream myfile;
      myfile.open ("error.txt", ios::app);
      cout << order << " " <<
              1./pow(2., ser_ref_levels*1.) << " " <<
              global_error << " " <<
              global_error_inf << " " <<
              NEglob << " " <<
              "k10-analytic-L2Error\n";
      myfile.close();
   }



   // Free the used memory.
   delete prec;
   delete S;
   delete nbcCoef;
   delete rhs_f;
   delete dist_vec;
   delete distance_vec_space;
   delete fec;

   MPI_Finalize();

   return 0;
}