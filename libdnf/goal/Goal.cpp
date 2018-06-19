/*
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <assert.h>
#include <map>

extern "C" {
#include <solv/evr.h>
#include <solv/queue.h>
#include <solv/selection.h>
#include <solv/solver.h>
#include <solv/solverdebug.h>
#include <solv/testcase.h>
#include <solv/transaction.h>
}

#include "Goal-private.hpp"
#include "../hy-goal-private.hpp"
#include "../hy-iutil-private.hpp"
#include "../hy-package-private.hpp"
#include "../dnf-sack-private.hpp"
#include "../sack/packageset.hpp"
#include "../sack/query.hpp"
#include "../sack/selector.hpp"
#include "../utils/bgettext/bgettext-lib.h"
#include "../utils/tinyformat/tinyformat.hpp"
#include "IdQueue.hpp"

enum {NO_MATCH=1, MULTIPLE_MATCH_OBJECTS, INCORECT_COMPARISON_TYPE};

static std::map<int, const char *> ERROR_DICT = {
   {MULTIPLE_MATCH_OBJECTS, M_("Ill-formed Selector, presence of multiple match objects in the filter")},
   {INCORECT_COMPARISON_TYPE, M_("Ill-formed Selector used for the operation, incorrect comparison type")}
};

static void
packageToJob(DnfPackage * package, Queue * job, int solver_action)
{
    libdnf::IdQueue pkgs;

    Pool *pool = dnf_package_get_pool(package);
    DnfSack *sack = dnf_package_get_sack(package);

    dnf_sack_recompute_considered(sack);
    dnf_sack_make_provides_ready(sack);

    pkgs.pushBack(dnf_package_get_id(package));

    Id what = pool_queuetowhatprovides(pool, pkgs.getQueue());
    queue_push2(job, SOLVER_SOLVABLE_ONE_OF|SOLVER_SETARCH|SOLVER_SETEVR|solver_action, what);
}

static int
jobHas(Queue *job, Id what, Id id)
{
    for (int i = 0; i < job->count; i += 2)
        if (job->elements[i] == what && job->elements[i + 1] == id)
            return 1;
    return 0;
}

static int
filterArchToJob(DnfSack *sack, const libdnf::Filter *f, Queue *job)
{
    if (!f)
        return 0;

    auto matches = f->getMatches();

    if (f->getCmpType() != HY_EQ) {
        return INCORECT_COMPARISON_TYPE;
    }
    if (matches.size() != 1) {
        return MULTIPLE_MATCH_OBJECTS;
    }
    Pool *pool = dnf_sack_get_pool(sack);
    const char *arch = matches[0].str;
    Id archid = str2archid(pool, arch);

    if (archid == 0)
        return NO_MATCH;
    for (int i = 0; i < job->count; i += 2) {
        Id dep;
        assert((job->elements[i] & SOLVER_SELECTMASK) == SOLVER_SOLVABLE_NAME);
        dep = pool_rel2id(pool, job->elements[i + 1],
                          archid, REL_ARCH, 1);
        job->elements[i] |= SOLVER_SETARCH;
        job->elements[i + 1] = dep;
    }
    return 0;
}

static int
filterEvrToJob(DnfSack *sack, const libdnf::Filter *f, Queue *job)
{
    if (!f)
        return 0;
    auto matches = f->getMatches();

    if (f->getCmpType() != HY_EQ) {
        return INCORECT_COMPARISON_TYPE;
    }
    if (matches.size() != 1) {
        return MULTIPLE_MATCH_OBJECTS;
    }

    Pool *pool = dnf_sack_get_pool(sack);
    Id evr = pool_str2id(pool, matches[0].str, 1);
    Id constr = f->getKeyname() == HY_PKG_VERSION ? SOLVER_SETEV : SOLVER_SETEVR;
    for (int i = 0; i < job->count; i += 2) {
        Id dep;
        assert((job->elements[i] & SOLVER_SELECTMASK) == SOLVER_SOLVABLE_NAME);
        dep = pool_rel2id(pool, job->elements[i + 1],
                          evr, REL_EQ, 1);
        job->elements[i] |= constr;
        job->elements[i + 1] = dep;
    }
    return 0;
}

static int
filterFileToJob(DnfSack *sack, const libdnf::Filter *f, Queue *job)
{
    if (!f)
        return 0;

    auto matches = f->getMatches();

    if (matches.size() != 1) {
        return MULTIPLE_MATCH_OBJECTS;
    }

    const char *file = matches[0].str;
    Pool *pool = dnf_sack_get_pool(sack);

    int flags = f->getCmpType() & HY_GLOB ? SELECTION_GLOB : 0;
    if (f->getCmpType() & HY_GLOB)
        flags |= SELECTION_NOCASE;
    if (selection_make(pool, job, file, flags | SELECTION_FILELIST) == 0)
        return NO_MATCH;
    return 0;
}

static int
filterPkgToJob(Id what, Queue *job)
{
    if (!what)
        return 0;
    queue_push2(job, SOLVER_SOLVABLE_ONE_OF|SOLVER_SETARCH|SOLVER_SETEVR, what);
    return 0;
}

static int
filterNameToJob(DnfSack *sack, const libdnf::Filter *f, Queue *job)
{
    if (!f)
        return 0;
    if (f->getMatches().size() != 1)
        return MULTIPLE_MATCH_OBJECTS;

    Pool *pool = dnf_sack_get_pool(sack);
    const char *name = f->getMatches()[0].str;
    Id id;
    Dataiterator di;

    switch (f->getCmpType()) {
    case HY_EQ:
        id = pool_str2id(pool, name, 0);
        if (id)
            queue_push2(job, SOLVER_SOLVABLE_NAME, id);
        break;
    case HY_GLOB:
        dataiterator_init(&di, pool, 0, 0, SOLVABLE_NAME, name, SEARCH_GLOB);
        while (dataiterator_step(&di)) {
            if (!is_package(pool, pool_id2solvable(pool, di.solvid)))
                continue;
            assert(di.idp);
            id = *di.idp;
            if (jobHas(job, SOLVABLE_NAME, id))
                continue;
            queue_push2(job, SOLVER_SOLVABLE_NAME, id);
        }
        dataiterator_free(&di);
        break;
    default:
        return INCORECT_COMPARISON_TYPE;
    }
    return 0;
}

static int
filterProvidesToJob(DnfSack *sack, const libdnf::Filter *f, Queue *job)
{
    if (!f)
        return 0;
    auto matches = f->getMatches();
    if (f->getMatches().size() != 1)
        return MULTIPLE_MATCH_OBJECTS;
    const char *name;
    Pool *pool = dnf_sack_get_pool(sack);
    Id id;
    Dataiterator di;

    switch (f->getCmpType()) {
        case HY_EQ:
            id = matches[0].reldep;
            queue_push2(job, SOLVER_SOLVABLE_PROVIDES, id);
            break;
        case HY_GLOB:
            name = matches[0].str;
            dataiterator_init(&di, pool, 0, 0, SOLVABLE_PROVIDES, name, SEARCH_GLOB);
            while (dataiterator_step(&di)) {
                if (is_package(pool, pool_id2solvable(pool, di.solvid)))
                    break;
            }
            assert(di.idp);
            id = *di.idp;
            if (!jobHas(job, SOLVABLE_PROVIDES, id))
                queue_push2(job, SOLVER_SOLVABLE_PROVIDES, id);
            dataiterator_free(&di);
            break;
        default:
            return INCORECT_COMPARISON_TYPE;
    }
    return 0;
}

static int
filterReponameToJob(DnfSack *sack, const libdnf::Filter *f, Queue *job)
{
    Id i;
    Repo *repo;

    if (!f)
        return 0;
    auto matches = f->getMatches();

    if (f->getCmpType() != HY_EQ) {
        return INCORECT_COMPARISON_TYPE;
    }
    if (matches.size() != 1) {
        return MULTIPLE_MATCH_OBJECTS;
    }

    libdnf::IdQueue repo_sel;
    Pool *pool = dnf_sack_get_pool(sack);
    FOR_REPOS(i, repo)
        if (!strcmp(matches[0].str, repo->name)) {
            repo_sel.pushBack(SOLVER_SOLVABLE_REPO | SOLVER_SETREPO, repo->repoid);
        }

    selection_filter(pool, job, repo_sel.getQueue());

    return 0;
}

/**
 * Build job queue from a Query.
 *
 * Returns an error code
 */
void
sltrToJob(const HySelector sltr, Queue *job, int solver_action)
{
    DnfSack *sack = sltr->getSack();
    int ret = 0;

    int any_opt_filter = sltr->getFilterArch() || sltr->getFilterEvr()
        || sltr->getFilterReponame();
    int any_req_filter = sltr->getFilterName() || sltr->getFilterProvides()
        || sltr->getFilterFile() || sltr->getPkgs();

    libdnf::IdQueue job_sltr;

    if (!any_req_filter) {
        if (any_opt_filter) {
            // no name or provides or file in the selector is an error
            throw libdnf::Goal::Exception("Ill-formed Selector. No name or"
                "provides or file in the selector.", DNF_ERROR_BAD_SELECTOR);
        }
        goto finish;
    }

    dnf_sack_recompute_considered(sack);
    dnf_sack_make_provides_ready(sack);
    ret = filterPkgToJob(sltr->getPkgs(), job_sltr.getQueue());
    if (ret)
        goto finish;
    ret = filterNameToJob(sack, sltr->getFilterName(), job_sltr.getQueue());
    if (ret)
        goto finish;
    ret = filterFileToJob(sack, sltr->getFilterFile(), job_sltr.getQueue());
    if (ret)
        goto finish;
    ret = filterProvidesToJob(sack, sltr->getFilterProvides(), job_sltr.getQueue());
    if (ret)
        goto finish;
    ret = filterArchToJob(sack, sltr->getFilterArch(), job_sltr.getQueue());
    if (ret)
        goto finish;
    ret = filterEvrToJob(sack, sltr->getFilterEvr(), job_sltr.getQueue());
    if (ret)
        goto finish;
    ret = filterReponameToJob(sack, sltr->getFilterReponame(), job_sltr.getQueue());
    if (ret)
        goto finish;

    for (int i = 0; i < job_sltr.size(); i += 2)
         queue_push2(job, job_sltr[i] | solver_action, job_sltr[i + 1]);

 finish:
    if (ret > 1) {
        throw libdnf::Goal::Exception(TM_(ERROR_DICT[ret], 1), DNF_ERROR_BAD_SELECTOR);
    }
}

namespace libdnf {

#define BLOCK_SIZE 15

struct InstallonliesSortCallback {
    Pool *pool;
    Id running_kernel;
};

static inline void
queue2pset(const IdQueue & queue, PackageSet * pset)
{
    for (int i = 0; i < queue.size(); ++i)
        pset->set(queue[i]);
}

static bool
/**
* @brief return false iff a does not depend on anything from b
*/
can_depend_on(Pool *pool, Solvable *sa, Id b)
{
    IdQueue requires;

    solvable_lookup_idarray(sa, SOLVABLE_REQUIRES, requires.getQueue());
    for (int i = 0; i < requires.size(); ++i) {
        Id req_dep = requires[i];
        Id p, pp;

        FOR_PROVIDES(p, pp, req_dep)
            if (p == b)
                return true;
    }

    return false;
}

static int
sort_packages(const void *ap, const void *bp, void *s_cb)
{
    Id a = *(Id*)ap;
    Id b = *(Id*)bp;
    Pool *pool = ((struct InstallonliesSortCallback*) s_cb)->pool;
    Id kernel = ((struct InstallonliesSortCallback*) s_cb)->running_kernel;
    Solvable *sa = pool_id2solvable(pool, a);
    Solvable *sb = pool_id2solvable(pool, b);

    /* if the names are different sort them differently, particular order does
       not matter as long as it's consistent. */
    int name_diff = sa->name - sb->name;
    if (name_diff)
        return name_diff;

    /* same name, if one is/depends on the running kernel put it last */

    /* move available packages to end of the list */
    if (pool->installed != sa->repo)
        return 1;

    if (pool->installed != sb->repo)
        return -1;

    if (kernel >= 0) {
        if (a == kernel || can_depend_on(pool, sa, kernel))
            return 1;
        if (b == kernel || can_depend_on(pool, sb, kernel))
            return -1;
    }

    return pool_evrcmp(pool, sa->evr, sb->evr, EVRCMP_COMPARE);
}

static void
same_name_subqueue(Pool *pool, Queue *in, Queue *out)
{
    Id el = queue_pop(in);
    Id name = pool_id2solvable(pool, el)->name;
    queue_empty(out);
    queue_push(out, el);
    while (in->count &&
           pool_id2solvable(pool, in->elements[in->count - 1])->name == name)
        // reverses the order so packages are sorted by descending version
        queue_push(out, queue_pop(in));
}

static std::unique_ptr<PackageSet>
remove_pkgs_with_same_nevra_from_pset(DnfPackageSet* pset, DnfPackageSet* remove_musters,
                                      DnfSack* sack)
{
    std::unique_ptr<PackageSet> final_pset(new PackageSet(sack));
    Id id1 = -1;
    while(true) {
        id1 = pset->next(id1);
        if (id1 == -1)
            break;
        DnfPackage *pkg1 = dnf_package_new(sack, id1);
        Id id2 = -1;
        bool found = false;
        while(true) {
            id2 = remove_musters->next(id2);
            if (id2 == -1)
                break;
            DnfPackage *pkg2 = dnf_package_new(sack, id2);
            if (!dnf_package_cmp(pkg1, pkg2)) {
                found = true;
                g_object_unref(pkg2);
                break;
            }
            g_object_unref(pkg2);
        }
        if (!found) {
            final_pset->set(pkg1);
        }
        g_object_unref(pkg1);
    }
    return final_pset;
}

static int
erase_flags2libsolv(int flags)
{
    int ret = 0;
    if (flags & HY_CLEAN_DEPS)
        ret |= SOLVER_CLEANDEPS;
    return ret;
}

Goal::Goal(const Goal & goal_src) : pImpl(new Impl(*goal_src.pImpl)) {}

Goal::Impl::Impl(const Goal::Impl & goal_src)
: sack(goal_src.sack)
{
    queue_init_clone(&staging, const_cast<Queue *>(&goal_src.staging));

    actions = goal_src.actions;
    if (goal_src.protectedPkgs) {
        protectedPkgs.reset(new PackageSet(*goal_src.protectedPkgs.get()));
    }
    if (goal_src.removalOfProtected) {
        removalOfProtected.reset(new PackageSet(*goal_src.removalOfProtected.get()));
    }
}

Goal::Impl::Impl(DnfSack *sack)
: sack(sack)
{
    queue_init(&staging);
}

Goal::Goal(DnfSack *sack) : pImpl(new Impl(sack)) {}

Goal::~Goal() = default;

Goal::Impl::~Impl()
{
    if (trans)
        transaction_free(trans);
    if (solv)
        solver_free(solv);
    queue_free(&staging);
}

DnfGoalActions Goal::getActions() { return pImpl->actions; }

DnfSack * Goal::getSack() { return pImpl->sack; }

int
Goal::getReason(DnfPackage *pkg)
{
    //solver_get_recommendations
    if (!pImpl->solv)
        return HY_REASON_USER;
    Id info;
    int reason = solver_describe_decision(pImpl->solv, dnf_package_get_id(pkg), &info);

    if ((reason == SOLVER_REASON_UNIT_RULE ||
         reason == SOLVER_REASON_RESOLVE_JOB) &&
        (solver_ruleclass(pImpl->solv, info) == SOLVER_RULE_JOB ||
         solver_ruleclass(pImpl->solv, info) == SOLVER_RULE_BEST))
        return HY_REASON_USER;
    if (reason == SOLVER_REASON_CLEANDEPS_ERASE)
        return HY_REASON_CLEAN;
    if (reason == SOLVER_REASON_WEAKDEP)
        return HY_REASON_WEAKDEP;
    return HY_REASON_DEP;
}

void
Goal::addProtected(PackageSet & pset)
{
    if (!pImpl->protectedPkgs) {
        pImpl->protectedPkgs.reset(new PackageSet(pset));
    } else {
        map_or(pImpl->protectedPkgs->getMap(), pset.getMap());
    }
}

void
Goal::setProtected(const PackageSet & pset)
{
    pImpl->protectedPkgs.reset(new PackageSet(pset));
}

void
Goal::distupgrade()
{
    pImpl->actions = static_cast<DnfGoalActions>(pImpl->actions | DNF_DISTUPGRADE);
    DnfSack * sack = pImpl->sack;
    Query query(sack);
    query.addFilter(HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
    Selector selector(sack);
    selector.set(query.runSet());
    sltrToJob(&selector, &pImpl->staging, SOLVER_DISTUPGRADE);
}

void
Goal::distupgrade(DnfPackage *new_pkg)
{
    pImpl->actions = static_cast<DnfGoalActions>(pImpl->actions | DNF_DISTUPGRADE);
    packageToJob(new_pkg, &pImpl->staging, SOLVER_DISTUPGRADE);
}

void
Goal::distupgrade(HySelector sltr)
{
    pImpl->actions = static_cast<DnfGoalActions>(pImpl->actions | DNF_DISTUPGRADE);
    sltrToJob(sltr, &pImpl->staging, SOLVER_DISTUPGRADE);
}

void
Goal::erase(DnfPackage *pkg, int flags)
{
    int additional = erase_flags2libsolv(flags);
    pImpl->actions = static_cast<DnfGoalActions>(pImpl->actions | DNF_ERASE);
    queue_push2(&pImpl->staging, SOLVER_SOLVABLE|SOLVER_ERASE|additional, dnf_package_get_id(pkg));
}

void
Goal::erase(HySelector sltr, int flags)
{
    int additional = erase_flags2libsolv(flags);
    pImpl->actions = static_cast<DnfGoalActions>(pImpl->actions | DNF_ERASE);
    sltrToJob(sltr, &pImpl->staging, SOLVER_ERASE|additional);
}

void
Goal::install(DnfPackage *new_pkg, bool optional)
{
    int solverActions = SOLVER_INSTALL;
    if (optional) {
        solverActions |= SOLVER_WEAK;
    }
    pImpl->actions = static_cast<DnfGoalActions>(pImpl->actions | DNF_INSTALL|DNF_ALLOW_DOWNGRADE);
    packageToJob(new_pkg, &pImpl->staging, solverActions);
}

void
Goal::install(HySelector sltr, bool optional)
{
    int solverActions = SOLVER_INSTALL;
    if (optional) {
        solverActions |= SOLVER_WEAK;
    }
    pImpl->actions = static_cast<DnfGoalActions>(pImpl->actions | DNF_INSTALL|DNF_ALLOW_DOWNGRADE);
    sltrToJob(sltr, &pImpl->staging, solverActions);
}

void
Goal::upgrade()
{
    pImpl->actions = static_cast<DnfGoalActions>(pImpl->actions | DNF_UPGRADE_ALL);
    queue_push2(&pImpl->staging, SOLVER_UPDATE|SOLVER_SOLVABLE_ALL, 0);
}

void
Goal::upgrade(DnfPackage *new_pkg)
{
    pImpl->actions = static_cast<DnfGoalActions>(pImpl->actions | DNF_UPGRADE);
    packageToJob(new_pkg, &pImpl->staging, SOLVER_UPDATE);
}

void
Goal::upgrade(HySelector sltr)
{
    pImpl->actions = static_cast<DnfGoalActions>(pImpl->actions | DNF_UPGRADE);
    sltrToJob(sltr, &pImpl->staging, SOLVER_UPDATE);
}

void
Goal::userInstalled(DnfPackage *pkg)
{
    queue_push2(&pImpl->staging, SOLVER_SOLVABLE|SOLVER_USERINSTALLED, dnf_package_get_id(pkg));
}

void
Goal::userInstalled(PackageSet & pset)
{
    Id id = -1;
    while (true) {
        id = pset.next(id);
        if (id == -1)
            break;
        queue_push2(&pImpl->staging, SOLVER_SOLVABLE|SOLVER_USERINSTALLED, id);
    }
}

bool
Goal::hasActions(DnfGoalActions action)
{
    return pImpl->actions & action;
}

int
Goal::jobLength()
{
    return (&pImpl->staging)->count / 2;
}

int
Goal::run(DnfGoalActions flags)
{
    auto job = pImpl->constructJob(flags);
    pImpl->actions = static_cast<DnfGoalActions>(pImpl->actions | flags);
    int ret = pImpl->solve(job->getQueue(), flags);
    return ret;
}

int
Goal::countProblems()
{
    return pImpl->countProblems();
}

/**
 * Reports packages that has a conflict
 *
 * available - if available it returns set with available packages with conflicts
 * available - if package installed it also excludes available packages with same NEVRA
 *
 * Returns DnfPackageSet with all packages that have a conflict.
 */
std::unique_ptr<PackageSet>
Goal::listConflictPkgs(DnfPackageState pkg_type)
{
    DnfSack * sack = pImpl->sack;
    Pool * pool = dnf_sack_get_pool(sack);
    std::unique_ptr<PackageSet> pset(new PackageSet(sack));
    PackageSet temporary_pset(sack);

    int countProblemsValue = pImpl->countProblems();
    for (int i = 0; i < countProblemsValue; i++) {
        auto conflict = pImpl->conflictPkgs(i);
        for (int j = 0; j < conflict->size(); j++) {
            Id id = (*conflict)[j];
            Solvable *s = pool_id2solvable(pool, id);
            bool installed = pool->installed == s->repo;
            if (pkg_type ==  DNF_PACKAGE_STATE_AVAILABLE && installed) {
                temporary_pset.set(id);
                continue;
            }
            if (pkg_type ==  DNF_PACKAGE_STATE_INSTALLED && !installed)
                continue;
            pset->set(id);
        }
    }
    if (!temporary_pset.size()) {
        return pset;
    }

    return remove_pkgs_with_same_nevra_from_pset(pset.get(), &temporary_pset, sack);
}

/**
 * Reports all packages that have broken dependency
 * available - if available returns only available packages with broken dependencies
 * available - if package installed it also excludes available packages with same NEVRA
 * Returns DnfPackageSet with all packages with broken dependency
 */
std::unique_ptr<PackageSet>
Goal::listBrokenDependencyPkgs(DnfPackageState pkg_type)
{
    return pImpl->brokenDependencyAllPkgs(pkg_type);
}


char **
Goal::describeProblemRules(unsigned i)
{
    char **problist = NULL;
    int p = 0;
    /* internal error */
    if (i >= (unsigned) pImpl->countProblems())
        return NULL;
    // problem is not in libsolv - removal of protected packages
    char *problem = pImpl->describeProtectedRemoval();
    if (problem) {
        problist = (char**)solv_extend(problist, p, 1, sizeof(char*), BLOCK_SIZE);
        problist[p++] = problem;
        problist = (char**)solv_extend(problist, p, 1, sizeof(char*), BLOCK_SIZE);
        problist[p++] = NULL;
        return problist;
    }

    Id rid, source, target, dep;
    SolverRuleinfo type;
    int j;
    bool unique;

    if (i >= solver_problem_count(pImpl->solv))
        return problist;

    IdQueue pq;
    IdQueue rq;
    // this libsolv interface indexes from 1 (we do from 0), so:
    solver_findallproblemrules(pImpl->solv, i+1, pq.getQueue());
    for (j = 0; j < pq.size(); j++) {
        rid = pq[j];
        if (solver_allruleinfos(pImpl->solv, rid, rq.getQueue())) {
            for (int ir = 0; ir < rq.size(); ir+=4) {
                type = static_cast<SolverRuleinfo>(rq[ir]);
                source = rq[ir + 1];
                target = rq[ir + 2];
                dep = rq[ir + 3];
                const char *problem_str = solver_problemruleinfo2str(
                    pImpl->solv, type, source, target, dep);
                unique = true;
                if (problist != NULL) {
                    for (int k = 0; problist[k] != NULL; k++) {
                        if (g_strcmp0(problem_str, problist[k]) == 0) {
                            unique = false;
                            break;
                        }
                    }
                }
                if (unique) {
                    if (!problist)
                        problist = (char**)solv_extend(problist, p, 1, sizeof(char*), BLOCK_SIZE);
                    problist[p++] = g_strdup(problem_str);
                    problist = (char**)solv_extend(problist, p, 1, sizeof(char*), BLOCK_SIZE);
                    problist[p] = NULL;
                }
            }
        }
    }
    return problist;
}

/**
 * Write all the solving decisions to the hawkey logfile.
 */
int
Goal::logDecisions()
{
    if (!pImpl->solv)
        return 1;
    solver_printdecisionq(pImpl->solv, SOLV_DEBUG_RESULT);
    return 0;
}

/**
 * hy_goal_write_debugdata:
 * @goal: A #HyGoal
 * @dir: The directory to write to
 * @error: A #GError, or %NULL
 *
 * Writes details about the testcase to a directory.
 *
 * Returns: %false if an error was set
 *
 * Since: 0.7.0
 */
void
Goal::writeDebugdata(const char *dir)
{
    Solver *solv = pImpl->solv;
    if (!solv) {
        throw Goal::Exception(_("no solver set"), DNF_ERROR_INTERNAL_ERROR);
    }

    int flags = TESTCASE_RESULT_TRANSACTION | TESTCASE_RESULT_PROBLEMS;
    g_autofree char *absdir = abspath(dir);
    if (!absdir) {
        std::string msg = tfm::format(_("failed to make %s absolute"), dir);
        throw Goal::Exception(msg, DNF_ERROR_FILE_INVALID);
    }
    g_debug("writing solver debugdata to %s", absdir);
    int ret = testcase_write(solv, absdir, flags, NULL, NULL);
    if (!ret) {
        std::string msg = tfm::format(_("failed writing debugdata to %1$s: %2$s"),
                                      absdir, strerror(errno));
        throw Goal::Exception(msg, DNF_ERROR_FILE_INVALID);
    }
}

PackageSet
Goal::Impl::listResults(Id type_filter1, Id type_filter2)
{
    /* no transaction */
    if (!trans) {
        if (!solv) {
            throw Goal::Exception(_("no solv in the goal"), DNF_ERROR_INTERNAL_ERROR);
        } else if (removalOfProtected && removalOfProtected->size()) {
            throw Goal::Exception(_("no solution, cannot remove protected package"),
                                          DNF_ERROR_REMOVAL_OF_PROTECTED_PKG);
        }
        throw Goal::Exception(_("no solution possible"), DNF_ERROR_NO_SOLUTION);
    }
    PackageSet plist(sack);
    const int common_mode = SOLVER_TRANSACTION_SHOW_OBSOLETES |
        SOLVER_TRANSACTION_CHANGE_IS_REINSTALL;

    for (int i = 0; i < trans->steps.count; ++i) {
        Id p = trans->steps.elements[i];
        Id type;

        switch (type_filter1) {
        case SOLVER_TRANSACTION_OBSOLETED:
            type =  transaction_type(trans, p, common_mode);
            break;
        default:
            type  = transaction_type(trans, p, common_mode |
                                     SOLVER_TRANSACTION_SHOW_ACTIVE|
                                     SOLVER_TRANSACTION_SHOW_ALL);
            break;
        }

        if (type == type_filter1 || (type_filter2 && type == type_filter2))
            plist.set(p);
    }
    return plist;
}

PackageSet
Goal::listErasures()
{
    return pImpl->listResults(SOLVER_TRANSACTION_ERASE, 0);
}

PackageSet
Goal::listInstalls()
{
    return pImpl->listResults(SOLVER_TRANSACTION_INSTALL, SOLVER_TRANSACTION_OBSOLETES);
}

PackageSet
Goal::listObsoleted()
{
    return pImpl->listResults(SOLVER_TRANSACTION_OBSOLETED, 0);
}

PackageSet
Goal::listReinstalls()
{
    return pImpl->listResults(SOLVER_TRANSACTION_REINSTALL, 0);
}

PackageSet
Goal::listUnneeded()
{
    PackageSet pset(pImpl->sack);
    IdQueue queue;
    Solver *solv = pImpl->solv;

    solver_get_unneeded(solv, queue.getQueue(), 0);
    queue2pset(queue, &pset);
    return pset;
}

PackageSet
Goal::listUpgrades()
{
    return pImpl->listResults(SOLVER_TRANSACTION_UPGRADE, 0);
}

PackageSet
Goal::listDowngrades()
{
    return pImpl->listResults(SOLVER_TRANSACTION_DOWNGRADE, 0);
}

PackageSet
Goal::listObsoletedByPackage(DnfPackage *pkg)
{
    auto trans = pImpl->trans;
    IdQueue obsoletes;
    PackageSet pset(pImpl->sack);

    assert(trans);

    transaction_all_obs_pkgs(trans, dnf_package_get_id(pkg), obsoletes.getQueue());
    queue2pset(obsoletes, &pset);

    return pset;
}

void
Goal::Impl::allowUninstallAllButProtected(Queue *job, DnfGoalActions flags)
{
    Pool *pool = dnf_sack_get_pool(sack);

    if (!protectedPkgs) {
        protectedPkgs.reset(new PackageSet(sack));
    } else
        map_grow(protectedPkgs->getMap(), pool->nsolvables);

    Id kernel = dnf_sack_running_kernel(sack);
    if (kernel > 0)
        protectedPkgs->set(kernel);

    if (DNF_ALLOW_UNINSTALL & flags)
        for (Id id = 1; id < pool->nsolvables; ++id) {
            Solvable *s = pool_id2solvable(pool, id);
            if (pool->installed == s->repo && !protectedPkgs->has(id) &&
                (!pool->considered || MAPTST(pool->considered, id))) {
                queue_push2(job, SOLVER_ALLOWUNINSTALL|SOLVER_SOLVABLE, id);
            }
        }
}

std::unique_ptr<IdQueue>
Goal::Impl::constructJob(DnfGoalActions flags)
{
    std::unique_ptr<IdQueue> job(new IdQueue(staging));
    auto elements = job->data();
    /* apply forcebest */
    if (flags & DNF_FORCE_BEST)
        for (int i = 0; i < job->size(); i += 2) {
            elements[i] |= SOLVER_FORCEBEST;
    }

    /* turn off implicit obsoletes for installonly packages */
    for (int i = 0; i < (int) dnf_sack_get_installonly(sack)->count; i++)
        job->pushBack(SOLVER_MULTIVERSION|SOLVER_SOLVABLE_PROVIDES,
            dnf_sack_get_installonly(sack)->elements[i]);

    allowUninstallAllButProtected(job->getQueue(), flags);

    if (flags & DNF_VERIFY)
        job->pushBack(SOLVER_VERIFY|SOLVER_SOLVABLE_ALL, 0);

    return job;
}

Solver *
Goal::Impl::initSolver()
{
    Pool *pool = dnf_sack_get_pool(sack);
    Solver *solvNew = solver_create(pool);

    if (solv)
        solver_free(solv);
    solv = solvNew;

    /* no vendor locking */
    solver_set_flag(solv, SOLVER_FLAG_ALLOW_VENDORCHANGE, 1);
    /* don't erase packages that are no longer in repo during distupgrade */
    solver_set_flag(solv, SOLVER_FLAG_KEEP_ORPHANS, 1);
    /* no arch change for forcebest */
    solver_set_flag(solv, SOLVER_FLAG_BEST_OBEY_POLICY, 1);
    /* support package splits via obsoletes */
    solver_set_flag(solv, SOLVER_FLAG_YUM_OBSOLETES, 1);

#if defined(LIBSOLV_FLAG_URPMREORDER)
    /* support urpm-like solution reordering */
    solver_set_flag(solv, SOLVER_FLAG_URPM_REORDER, 1);
#endif

    return solv;
}

int
Goal::Impl::limitInstallonlyPackages(Solver *solv, Queue *job)
{
    if (!dnf_sack_get_installonly_limit(sack))
        return 0;

    Queue *onlies = dnf_sack_get_installonly(sack);
    Pool *pool = dnf_sack_get_pool(sack);
    int reresolve = 0;

    for (int i = 0; i < onlies->count; ++i) {
        Id p, pp;
        IdQueue q, installing;

        FOR_PKG_PROVIDES(p, pp, onlies->elements[i])
            if (solver_get_decisionlevel(solv, p) > 0)
                q.pushBack(p);
        if (q.size() <= (int) dnf_sack_get_installonly_limit(sack)) {
            continue;
        }
        for (int k = 0; k < q.size(); ++k) {
            Id id  = q[k];
            Solvable *s = pool_id2solvable(pool, id);
            if (pool->installed != s->repo) {
                installing.pushBack(id);
                break;
            }
        }
        if (!installing.size()) {
            continue;
        }

        struct InstallonliesSortCallback s_cb = {pool, dnf_sack_running_kernel(sack)};
        solv_sort(q.data(), q.size(), sizeof(q[0]), sort_packages, &s_cb);
        IdQueue same_names;
        while (q.size() > 0) {
            same_name_subqueue(pool, q.getQueue(), same_names.getQueue());
            if (same_names.size() <= (int) dnf_sack_get_installonly_limit(sack))
                continue;
            reresolve = 1;
            for (int j = 0; j < same_names.size(); ++j) {
                Id id  = same_names[j];
                Id action = SOLVER_ERASE;
                if (j < (int) dnf_sack_get_installonly_limit(sack))
                    action = SOLVER_INSTALL;
                queue_push2(job, action | SOLVER_SOLVABLE, id);
            }
        }
    }
    return reresolve;
}

int
Goal::Impl::solve(Queue *job, DnfGoalActions flags)
{
    /* apply the excludes */
    dnf_sack_recompute_considered(sack);

    dnf_sack_make_provides_ready(sack);
    if (trans) {
        transaction_free(trans);
        trans = NULL;
    }

    Solver *solv = initSolver();

    /* Removal of SOLVER_WEAK to allow report errors*/
    if (DNF_IGNORE_WEAK & flags) {
        for (int i = 0; i < job->count; i += 2) {
            job->elements[i] &= ~SOLVER_WEAK;
        }
    }

    if (DNF_IGNORE_WEAK_DEPS & flags)
        solver_set_flag(solv, SOLVER_FLAG_IGNORE_RECOMMENDED, 1);

    if (DNF_ALLOW_DOWNGRADE & actions)
        solver_set_flag(solv, SOLVER_FLAG_ALLOW_DOWNGRADE, 1);

    if (solver_solve(solv, job))
        return 1;
    // either allow solutions callback or installonlies, both at the same time
    // are not supported
    if (limitInstallonlyPackages(solv, job)) {
        // allow erasing non-installonly packages that depend on a kernel about
        // to be erased
        allowUninstallAllButProtected(job, DNF_ALLOW_UNINSTALL);
        if (solver_solve(solv, job))
            return 1;
    }
    trans = solver_create_transaction(solv);

    if (protectedInRemovals())
        return 1;

    return 0;
}

/**
 * Reports packages that has a conflict
 *
 * Returns Queue with Ids of packages with conflict
 */
std::unique_ptr<IdQueue>
Goal::Impl::conflictPkgs(unsigned i)
{
    SolverRuleinfo type;
    Id rid, source, target, dep;
    std::unique_ptr<IdQueue> conflict(new IdQueue);
    if (i >= solver_problem_count(solv))
        return conflict;

    IdQueue pq;
    // this libsolv interface indexes from 1 (we do from 0), so:
    solver_findallproblemrules(solv, i+1, pq.getQueue());
    for ( int j = 0; j < pq.size(); j++) {
        rid = pq[j];
        type = solver_ruleinfo(solv, rid, &source, &target, &dep);
        if (type == SOLVER_RULE_PKG_CONFLICTS)
            conflict->pushBack(source, target);
        else if (type == SOLVER_RULE_PKG_SELF_CONFLICT)
            conflict->pushBack(source);
        else if (type == SOLVER_RULE_PKG_SAME_NAME)
            conflict->pushBack(source, target);
    }
    return conflict;
}

int
Goal::Impl::countProblems()
{
    assert(solv);
    size_t protectedSize = removalOfProtected ? removalOfProtected->size() : 0;
    return solver_problem_count(solv) + MIN(1, protectedSize);
}

std::unique_ptr<PackageSet>
Goal::Impl::brokenDependencyAllPkgs(DnfPackageState pkg_type)
{
    Pool * pool = dnf_sack_get_pool(sack);

    std::unique_ptr<PackageSet> pset(new PackageSet(sack));
    PackageSet temporary_pset(sack);

    int countProblemsValue = countProblems();
    for (int i = 0; i < countProblemsValue; i++) {
        auto broken_dependency = brokenDependencyPkgs(i);
        for (int j = 0; j < broken_dependency->size(); j++) {
            Id id = (*broken_dependency)[j];
            Solvable *s = pool_id2solvable(pool, id);
            bool installed = pool->installed == s->repo;
            if (pkg_type ==  DNF_PACKAGE_STATE_AVAILABLE && installed) {
                temporary_pset.set(id);
                continue;
            }
            if (pkg_type ==  DNF_PACKAGE_STATE_INSTALLED && !installed)
                continue;
            pset->set(id);
        }
    }
    if (!temporary_pset.size()) {
        return pset;
    }
    return remove_pkgs_with_same_nevra_from_pset(pset.get(), &temporary_pset, sack);
}

/**
 * Reports packages that have broken dependency
 *
 * Returns Queue with Ids of packages with broken dependency
 */
std::unique_ptr<IdQueue>
Goal::Impl::brokenDependencyPkgs(unsigned i)
{
    SolverRuleinfo type;
    Id rid, source, target, dep;

    auto broken_dependency = std::unique_ptr<IdQueue>(new IdQueue);
    if (i >= solver_problem_count(solv))
        return broken_dependency;
    IdQueue pq;
    // this libsolv interface indexes from 1 (we do from 0), so:
    solver_findallproblemrules(solv, i+1, pq.getQueue());
    for (int j = 0; j < pq.size(); j++) {
        rid = pq[j];
        type = solver_ruleinfo(solv, rid, &source, &target, &dep);
        if (type == SOLVER_RULE_PKG_NOTHING_PROVIDES_DEP)
            broken_dependency->pushBack(source);
        else if (type == SOLVER_RULE_PKG_REQUIRES)
            broken_dependency->pushBack(source);
    }
    return broken_dependency;
}

bool
Goal::Impl::protectedInRemovals()
{
    guint i = 0;
    bool ret = false;
    if (!protectedPkgs || !protectedPkgs->size())
        return false;
    auto pkgRemoveList = listResults(SOLVER_TRANSACTION_ERASE, 0);
    auto pkgObsoleteList = listResults(SOLVER_TRANSACTION_OBSOLETED, 0);
    map_or(pkgRemoveList.getMap(), pkgObsoleteList.getMap());

    removalOfProtected.reset(new PackageSet(pkgRemoveList));
    Id id = -1;
    while(true) {
        id = removalOfProtected->next(id);
        if (id == -1)
            break;

        if (protectedPkgs->has(id)) {
            ret = true;
            i++;
        } else {
            removalOfProtected->remove(id);
        }
    }
    return ret;
}

/**
 * String describing the removal of protected packages.
 *
 * Caller is responsible for freeing the returned string using g_free().
 */
char *
Goal::Impl::describeProtectedRemoval()
{
    g_autoptr(GString) string = NULL;
    bool firstElement = true;
    const char *name;
    Pool *pool = solv->pool;
    Solvable *s;

    string = g_string_new(_("The operation would result in removing"
                            " the following protected packages: "));

    if (removalOfProtected && removalOfProtected->size()) {
        Id id = -1;
        while(true) {
            id = removalOfProtected->next(id);
            if (id == -1)
                break;
            Solvable * s = pool_id2solvable(pool, id);
            name = pool_id2str(pool, s->name);
            if (firstElement) {
                g_string_append(string, name);
                firstElement = false;
            } else {
                g_string_append_printf(string, ", %s", name);
            }
        }
        return g_strdup(string->str);
    }
    auto pset = brokenDependencyAllPkgs(DNF_PACKAGE_STATE_INSTALLED);
    Id id = -1;
    bool found = false;
    while(true) {
        id = pset->next(id);
        if (id == -1)
            break;
        if (protectedPkgs->has(id)) {
            s = pool_id2solvable(pool, id);
            name = pool_id2str(pool, s->name);
            if (!found) {
                g_string_append(string, name);
                found = true;
            } else {
                g_string_append_printf(string, ", %s", name);
            }
        }
    }
    if (found)
        return g_strdup(string->str);
    return NULL;
}

}
