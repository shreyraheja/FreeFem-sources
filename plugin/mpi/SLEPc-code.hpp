#include "PETSc.hpp"

#ifdef WITH_slepc
#define WITH_SLEPC
#endif
#ifdef WITH_slepccomplex
#define WITH_SLEPC
#endif

#ifdef WITH_SLEPC

#include "slepc.h"

namespace SLEPc {
template<class Type, class K>
struct _n_User;
template<class Type, class K>
using User = _n_User<Type, K>*;
template<class Type, class K>
static PetscErrorCode MatMult_User(Mat A, Vec x, Vec y);
template<class K, typename std::enable_if<std::is_same<K, double>::value || !std::is_same<PetscScalar, double>::value>::type* = nullptr>
void copy(K* pt, PetscInt n, PetscScalar* xr, PetscScalar* xi) { }
template<class K, typename std::enable_if<!std::is_same<K, double>::value && std::is_same<PetscScalar, double>::value>::type* = nullptr>
void copy(K* pt, PetscInt n, PetscScalar* xr, PetscScalar* xi) {
    for(int i = 0; i < n; ++i)
        pt[i] = K(xr[i], xi[i]);
}
template<class K, typename std::enable_if<std::is_same<K, double>::value || !std::is_same<PetscScalar, double>::value>::type* = nullptr>
void assign(K* pt, PetscScalar& kr, PetscScalar& ki) {
    *pt = kr;
}
template<class K, typename std::enable_if<!std::is_same<K, double>::value && std::is_same<PetscScalar, double>::value>::type* = nullptr>
void assign(K* pt, PetscScalar& kr, PetscScalar& ki) {
    *pt = K(kr, ki);
}
template<class K, typename std::enable_if<(std::is_same<PetscScalar, double>::value && std::is_same<K, std::complex<double>>::value)>::type* = nullptr>
void distributedVec(unsigned int* num, unsigned int first, unsigned int last, K* const in, PetscScalar* pt, unsigned int n) { }
template<class K, typename std::enable_if<!(std::is_same<PetscScalar, double>::value && std::is_same<K, std::complex<double>>::value)>::type* = nullptr>
void distributedVec(unsigned int* num, unsigned int first, unsigned int last, K* const in, PetscScalar* pt, unsigned int n) {
    HPDDM::Subdomain<K>::template distributedVec<0>(num, first, last, in, pt, n, 1);
}
template<class Type, class K>
class eigensolver : public OneOperator {
    public:
        typedef KN<PetscScalar> Kn;
        typedef KN_<PetscScalar> Kn_;
        class MatF_O : public RNM_VirtualMatrix<PetscScalar> {
            public:
                Stack stack;
                mutable Kn x;
                C_F0 c_x;
                Expression mat;
                typedef typename RNM_VirtualMatrix<PetscScalar>::plusAx plusAx;
                MatF_O(int n, Stack stk, const OneOperator* op) :
                    RNM_VirtualMatrix<PetscScalar>(n), stack(stk), x(n), c_x(CPValue(x)),
                    mat(op ? CastTo<Kn_>(C_F0(op->code(basicAC_F0_wa(c_x)), (aType)*op)) : 0) { }
                ~MatF_O() {
                    delete mat;
                    Expression zzz = c_x;
                    delete zzz;
                }
                void addMatMul(const Kn_& xx, Kn_& Ax) const {
                    ffassert(xx.N() == Ax.N());
                    x = xx;
                    Ax += GetAny<Kn_>((*mat)(stack));
                    WhereStackOfPtr2Free(stack)->clean();
                }
                plusAx operator*(const Kn& x) const { return plusAx(this, x); }
                bool ChecknbLine(int) const { return true; }
                bool ChecknbColumn(int) const { return true; }
        };
        const int c;
        class E_eigensolver : public E_F0mps {
            public:
                Expression A;
                Expression B;
                const OneOperator* codeA;
                const int c;
                static const int n_name_param = 9;
                static basicAC_F0::name_and_type name_param[];
                Expression nargs[n_name_param];
                E_eigensolver(const basicAC_F0& args, int d) : A(0), B(0), codeA(0), c(d) {
                    args.SetNameParam(n_name_param, name_param, nargs);
                    A = to<Type*>(args[0]);
                    if(c == 1) {
                        const Polymorphic* op = dynamic_cast<const Polymorphic*>(args[1].LeftValue());
                        ffassert(op);
                        codeA = op->Find("(", ArrayOfaType(atype<KN<PetscScalar>*>(), false));
                    }
                    else {
                        B = to<Type*>(args[1]);
                    }
                }

                AnyType operator()(Stack stack) const;
                operator aType() const { return atype<long>(); }
        };
        E_F0* code(const basicAC_F0 & args) const { return new E_eigensolver(args, c); }
        eigensolver() : OneOperator(atype<long>(), atype<Type*>(), atype<Type*>()), c(0) { }
        eigensolver(int) : OneOperator(atype<long>(), atype<Type*>(), atype<Polymorphic*>()), c(1) { }
};
template<class Type, class K>
basicAC_F0::name_and_type eigensolver<Type, K>::E_eigensolver::name_param[] = {
    {"sparams", &typeid(std::string*)},
    {"prefix", &typeid(std::string*)},
    {"values", &typeid(KN<K>*)},
    {"vectors", &typeid(FEbaseArrayKn<K>*)},
    {"array", &typeid(KNM<K>*)},
    {"fields", &typeid(KN<double>*)},
    {"names", &typeid(KN<String>*)},
    {"schurPreconditioner", &typeid(KN<Matrice_Creuse<HPDDM::upscaled_type<PetscScalar>>>*)},
    {"schurList", &typeid(KN<double>*)}
};
template<class Type, class K>
struct _n_User {
    typename eigensolver<Type, K>::MatF_O* mat;
};
template<class Type, class K>
AnyType eigensolver<Type, K>::E_eigensolver::operator()(Stack stack) const {
    ffassert(MAT_CLASSID);
    if(A && (B || codeA)) {
        Type* ptA = GetAny<Type*>((*A)(stack));
        if(ptA->_petsc) {
            EPS eps;
            EPSCreate(PETSC_COMM_WORLD, &eps);
            Mat S;
            User<Type, K> user = nullptr;
            MatType type;
            PetscBool isType;
            MatGetType(ptA->_petsc, &type);
            PetscStrcmp(type, MATNEST, &isType);
            PetscInt bs;
            PetscInt m;
            if(!codeA) {
                Type* ptB = GetAny<Type*>((*B)(stack));
                EPSSetOperators(eps, ptA->_petsc, ptB->_petsc);
                if(!ptA->_A) {
                    MatGetBlockSize(ptA->_petsc, &bs);
                    MatGetLocalSize(ptA->_petsc, &m, NULL);
                }
            }
            else {
                MatGetBlockSize(ptA->_petsc, &bs);
                MatGetLocalSize(ptA->_petsc, &m, NULL);
                PetscInt M;
                MatGetSize(ptA->_petsc, &M, NULL);
                PetscNew(&user);
                user->mat = new eigensolver<Type, K>::MatF_O(m * bs, stack, codeA);
                MatCreateShell(PETSC_COMM_WORLD, m, m, M, M, user, &S);
                MatShellSetOperation(S, MATOP_MULT, (void (*)(void))MatMult_User<Type, K>);
                EPSSetOperators(eps, S, NULL);
            }
            std::string* options = nargs[0] ? GetAny<std::string*>((*nargs[0])(stack)) : NULL;
            bool fieldsplit = PETSc::insertOptions(options);
            if(nargs[1])
                EPSSetOptionsPrefix(eps, GetAny<std::string*>((*nargs[1])(stack))->c_str());
            EPSSetFromOptions(eps);
            ST st;
            EPSGetST(eps, &st);
            if(ptA->_ksp)
                STSetKSP(st, ptA->_ksp);
            else if(fieldsplit) {
                KN<double>* fields = nargs[5] ? GetAny<KN<double>*>((*nargs[5])(stack)) : 0;
                KN<String>* names = nargs[6] ? GetAny<KN<String>*>((*nargs[6])(stack)) : 0;
                KN<Matrice_Creuse<HPDDM::upscaled_type<PetscScalar>>>* mS = nargs[7] ? GetAny<KN<Matrice_Creuse<HPDDM::upscaled_type<PetscScalar>>>*>((*nargs[7])(stack)) : 0;
                KN<double>* pL = nargs[8] ? GetAny<KN<double>*>((*nargs[8])(stack)) : 0;
                if(fields && names) {
                    KSP ksp;
                    STGetKSP(st, &ksp);
                    KSPSetOperators(ksp, ptA->_petsc, ptA->_petsc);
                    setFieldSplitPC(ptA, ksp, fields, names, mS, pL);
                    EPSSetUp(eps);
                    if(ptA->_vS && !ptA->_vS->empty()) {
                        PC pc;
                        KSPGetPC(ksp, &pc);
                        PCSetUp(pc);
                        PETSc::setCompositePC(pc, ptA->_vS);
                    }
                }
            }
            FEbaseArrayKn<K>* eigenvectors = nargs[3] ? GetAny<FEbaseArrayKn<K>*>((*nargs[3])(stack)) : nullptr;
            Vec* basis = nullptr;
            PetscInt n = 0;
            if(eigenvectors) {
                ffassert(!isType);
                if(eigenvectors->N > 0 && eigenvectors->get(0) && eigenvectors->get(0)->n > 0) {
                    n = eigenvectors->N;
                    basis = new Vec[n];
                    for(int i = 0; i < n; ++i) {
                        MatCreateVecs(ptA->_petsc, &basis[i], NULL);
                        PetscScalar* pt;
                        VecGetArray(basis[i], &pt);
                        if(!(std::is_same<PetscScalar, double>::value && std::is_same<K, std::complex<double>>::value))
                            distributedVec(ptA->_num, ptA->_first, ptA->_last, static_cast<K*>(*(eigenvectors->get(i))), pt, eigenvectors->get(i)->n);
                        VecRestoreArray(basis[i], &pt);
                    }
                }
                eigenvectors->resize(0);
            }
            if(n)
                EPSSetInitialSpace(eps, n, basis);
            EPSSolve(eps);
            for(int i = 0; i < n; ++i)
                VecDestroy(&basis[i]);
            delete [] basis;
            PetscInt nconv;
            EPSGetConverged(eps, &nconv);
            if(nconv > 0 && (nargs[2] || nargs[3] || nargs[4])) {
                KN<K>* eigenvalues = nargs[2] ? GetAny<KN<K>*>((*nargs[2])(stack)) : nullptr;
                KNM<K>* array = nargs[4] ? GetAny<KNM<K>*>((*nargs[4])(stack)) : nullptr;
                if(eigenvalues)
                    eigenvalues->resize(nconv);
                if(eigenvectors && !isType)
                    eigenvectors->resize(nconv);
                if(array)
                    array->resize(!codeA && !isType && ptA->_A ? ptA->_A->getDof() : m * bs, nconv);
                Vec xr, xi;
                PetscInt n;
                if(eigenvectors || array) {
                    MatCreateVecs(ptA->_petsc, PETSC_NULL, &xr);
                    MatCreateVecs(ptA->_petsc, PETSC_NULL, &xi);
                    VecGetLocalSize(xr, &n);
                }
                for(PetscInt i = 0; i < nconv; ++i) {
                    PetscScalar kr, ki;
                    EPSGetEigenpair(eps, i, &kr, &ki, (eigenvectors || array) ? xr : NULL, (eigenvectors || array) && std::is_same<PetscScalar, double>::value && std::is_same<K, std::complex<double>>::value ? xi : NULL);
                    if(eigenvectors || array) {
                        PetscScalar* tmpr;
                        PetscScalar* tmpi;
                        VecGetArray(xr, &tmpr);
                        K* pt;
                        if(std::is_same<PetscScalar, double>::value && std::is_same<K, std::complex<double>>::value) {
                            VecGetArray(xi, &tmpi);
                            pt = new K[n];
                            copy(pt, n, tmpr, tmpi);
                        }
                        else
                            pt = reinterpret_cast<K*>(tmpr);
                        if(!isType && ptA->_A) {
                            KN<K> cpy(ptA->_A->getDof());
                            cpy = K(0.0);
                            HPDDM::Subdomain<K>::template distributedVec<1>(ptA->_num, ptA->_first, ptA->_last, static_cast<K*>(cpy), pt, cpy.n, 1);
                            ptA->_A->HPDDM::template Subdomain<PetscScalar>::exchange(static_cast<K*>(cpy));
                            if(eigenvectors)
                                eigenvectors->set(i, cpy);
                            if(array && !codeA)
                                (*array)(':', i) = cpy;
                        }
                        if((codeA || isType || !ptA->_A) && array) {
                            KN<K> cpy(m * bs, pt);
                            (*array)(':', i) = cpy;
                        }
                        if(std::is_same<PetscScalar, double>::value && std::is_same<K, std::complex<double>>::value)
                            delete [] pt;
                        else
                            VecRestoreArray(xi, &tmpi);
                        VecRestoreArray(xr, &tmpr);
                    }
                    if(eigenvalues) {
                        if(sizeof(PetscScalar) == sizeof(K))
                            (*eigenvalues)[i] = kr;
                        else
                            assign(static_cast<K*>(*eigenvalues + i), kr, ki);

                    }
                }
                if(eigenvectors || array) {
                    VecDestroy(&xr);
                    VecDestroy(&xi);
                }
            }
            if(user) {
                MatDestroy(&S);
                delete user->mat;
                PetscFree(user);
            }
            EPSDestroy(&eps);
            return static_cast<long>(nconv);
        }
        else
            return 0L;
    }
    else
        return 0L;
}
template<class Type, class K>
static PetscErrorCode MatMult_User(Mat A, Vec x, Vec y) {
    User<Type, K>          user;
    const PetscScalar*       in;
    PetscScalar*            out;
    PetscErrorCode         ierr;

    PetscFunctionBegin;
    ierr = MatShellGetContext(A, &user); CHKERRQ(ierr);
    typename SLEPc::eigensolver<Type, K>::MatF_O* mat = reinterpret_cast<typename SLEPc::eigensolver<Type, K>::MatF_O*>(user->mat);
    VecGetArrayRead(x, &in);
    VecGetArray(y, &out);
    KN_<PetscScalar> xx(const_cast<PetscScalar*>(in), mat->N);
    KN_<PetscScalar> yy(out, mat->N);
    yy = *mat * xx;
    VecRestoreArray(y, &out);
    VecRestoreArrayRead(x, &in);
    PetscFunctionReturn(0);
}
void finalizeSLEPc() {
    PETSC_COMM_WORLD = MPI_COMM_WORLD;
    SlepcFinalize();
}
template<class K, typename std::enable_if<std::is_same<K, double>::value>::type* = nullptr>
void addSLEPc() {
    Global.Add("EPSSolveComplex", "(", new SLEPc::eigensolver<PETSc::DistributedCSR<HpSchwarz<PetscScalar>>, std::complex<double>>());
    Global.Add("EPSSolveComplex", "(", new SLEPc::eigensolver<PETSc::DistributedCSR<HpSchwarz<PetscScalar>>, std::complex<double>>(1));
}
template<class K, typename std::enable_if<!std::is_same<K, double>::value>::type* = nullptr>
void addSLEPc() { }
}

static void Init() {
    //  to load only once
    aType t;
    int r;
#ifdef WITH_slepccomplex
    const char * mmmm= "Petsc Slepc complex";
#else
    const char * mmmm= "Petsc Slepc real";
#endif
    if(!zzzfff->InMotClef(mmmm,t,r))
    {
#ifdef PETScandSLEPc
        Init_PETSc();
#endif
        int argc = pkarg->n;
        char** argv = new char*[argc];
        for(int i = 0; i < argc; ++i)
            argv[i] = const_cast<char*>((*(*pkarg)[i].getap())->c_str());
        PetscBool isInitialized;
        PetscInitialized(&isInitialized);
        ffassert(MAT_CLASSID);
        if(!isInitialized && mpirank == 0)
            std::cout << "PetscInitialize has not been called, do not forget to load PETSc before loading SLEPc" << std::endl;
        SlepcInitialize(&argc, &argv, 0, "");
        delete [] argv;
        ff_atend(SLEPc::finalizeSLEPc);
        SLEPc::addSLEPc<PetscScalar>();
        Global.Add("EPSSolve", "(", new SLEPc::eigensolver<PETSc::DistributedCSR<HpSchwarz<PetscScalar>>, PetscScalar>());
        Global.Add("EPSSolve", "(", new SLEPc::eigensolver<PETSc::DistributedCSR<HpSchwarz<PetscScalar>>, PetscScalar>(1));
        if(verbosity>1)cout << "*** End:: load PETSc & SELPc "<< typeid(PetscScalar).name() <<"\n\n"<<endl;
        zzzfff->Add(mmmm, atype<Dmat*>());
    }
    else {
        if(verbosity>1)cout << "*** reload and skip load PETSc & SELPc "<< typeid(PetscScalar).name() <<"\n\n"<<endl;
    }
}
#else
static void Init() {
     Init_PETSc();
}
#endif
