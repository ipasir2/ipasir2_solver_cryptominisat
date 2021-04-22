/******************************************
Copyright (C) 2009-2020 Authors of CryptoMiniSat, see AUTHORS file

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
***********************************************/

#include "reducedb.h"
#include "solver.h"
#include "solverconf.h"
#include "sqlstats.h"

// #define VERBOSE_DEBUG

#include <functional>
#include <cmath>

using namespace CMSat;

double safe_div(double a, double b) {
    if (b == 0) {
        assert(a == 0);
        return 0;
    }
    return a/b;
}

struct SortRedClsGlue
{
    explicit SortRedClsGlue(ClauseAllocator& _cl_alloc) :
        cl_alloc(_cl_alloc)
    {}
    ClauseAllocator& cl_alloc;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        const Clause* x = cl_alloc.ptr(xOff);
        const Clause* y = cl_alloc.ptr(yOff);
        return x->stats.glue < y->stats.glue;
    }
};

struct SortRedClsSize
{
    explicit SortRedClsSize(ClauseAllocator& _cl_alloc) :
        cl_alloc(_cl_alloc)
    {}
    ClauseAllocator& cl_alloc;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        const Clause* x = cl_alloc.ptr(xOff);
        const Clause* y = cl_alloc.ptr(yOff);
        return x->size() < y->size();
    }
};

struct SortRedClsAct
{
    explicit SortRedClsAct(ClauseAllocator& _cl_alloc) :
        cl_alloc(_cl_alloc)
    {}
    ClauseAllocator& cl_alloc;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        const Clause* x = cl_alloc.ptr(xOff);
        const Clause* y = cl_alloc.ptr(yOff);
        return x->stats.activity > y->stats.activity;
    }
};

#if defined(FINAL_PREDICTOR) || defined(STATS_NEEDED)
struct SortRedClsUIP1
{
    explicit SortRedClsUIP1(ClauseAllocator& _cl_alloc) :
        cl_alloc(_cl_alloc)
    {}
    ClauseAllocator& cl_alloc;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        const Clause* x = cl_alloc.ptr(xOff);
        const Clause* y = cl_alloc.ptr(yOff);
        return x->stats.uip1_used > y->stats.uip1_used;
    }
};

struct SortRedClsProps
{
    explicit SortRedClsProps(ClauseAllocator& _cl_alloc) :
        cl_alloc(_cl_alloc)
    {}
    ClauseAllocator& cl_alloc;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        const Clause* x = cl_alloc.ptr(xOff);
        const Clause* y = cl_alloc.ptr(yOff);
        return x->stats.props_made > y->stats.props_made;
    }
};

struct SortRedClsSumUIPPerTime
{
    explicit SortRedClsSumUIPPerTime(Solver* _solver) :
        solver(_solver)
    {

    }
    Solver* solver;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        const Clause* x = solver->cl_alloc.ptr(xOff);
        const Clause* y = solver->cl_alloc.ptr(yOff);
        const auto& x_extra_stats = solver->red_stats_extra[x->stats.extra_pos];
        const auto& y_extra_stats = solver->red_stats_extra[y->stats.extra_pos];

        return x_extra_stats.calc_sum_uip1_per_time(solver->sumConflicts) >
            y_extra_stats.calc_sum_uip1_per_time(solver->sumConflicts);
    }
};

struct SortRedClsSumPropsPerTime
{
    explicit SortRedClsSumPropsPerTime(Solver* _solver) :
        solver(_solver)
    {

    }
    Solver* solver;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        const Clause* x = solver->cl_alloc.ptr(xOff);
        const Clause* y = solver->cl_alloc.ptr(yOff);
        const auto& x_extra_stats = solver->red_stats_extra[x->stats.extra_pos];
        const auto& y_extra_stats = solver->red_stats_extra[y->stats.extra_pos];

        return x_extra_stats.calc_sum_props_per_time(solver->sumConflicts) >
            y_extra_stats.calc_sum_props_per_time(solver->sumConflicts);
    }
};

#endif

#ifdef FINAL_PREDICTOR
struct SortRedClsPredShort
{
    explicit SortRedClsPredShort(
        const ClauseAllocator& _cl_alloc,
        const vector<ClauseStatsExtra>& _extdata):
        cl_alloc(_cl_alloc),
        extdata(_extdata)
    {}
    const ClauseAllocator& cl_alloc;
    const vector<ClauseStatsExtra>& extdata;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        uint32_t x_num = cl_alloc.ptr(xOff)->stats.extra_pos;
        uint32_t y_num = cl_alloc.ptr(yOff)->stats.extra_pos;
        return extdata[x_num].pred_short_use > extdata[y_num].pred_short_use;
    }
};

struct SortRedClsPredLong
{
    explicit SortRedClsPredLong(
        const ClauseAllocator& _cl_alloc,
        const vector<ClauseStatsExtra>& _extdata):
        cl_alloc(_cl_alloc),
        extdata(_extdata)
    {}
    const ClauseAllocator& cl_alloc;
    const vector<ClauseStatsExtra>& extdata;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        uint32_t x_num = cl_alloc.ptr(xOff)->stats.extra_pos;
        uint32_t y_num = cl_alloc.ptr(yOff)->stats.extra_pos;
        return extdata[x_num].pred_long_use > extdata[y_num].pred_long_use;
    }
};

struct SortRedClsPredForever
{
    explicit SortRedClsPredForever(
        const ClauseAllocator& _cl_alloc,
        const vector<ClauseStatsExtra>& _extdata):
        cl_alloc(_cl_alloc),
        extdata(_extdata)
    {}
    const ClauseAllocator& cl_alloc;
    const vector<ClauseStatsExtra>& extdata;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        uint32_t x_num = cl_alloc.ptr(xOff)->stats.extra_pos;
        uint32_t y_num = cl_alloc.ptr(yOff)->stats.extra_pos;
        return extdata[x_num].pred_forever_use > extdata[y_num].pred_forever_use;
    }
};
#endif


ReduceDB::ReduceDB(Solver* _solver) :
    solver(_solver)
{
    cl_stats.resize(3);
}

ReduceDB::~ReduceDB()
{
    #ifdef FINAL_PREDICTOR
    delete predictors;
    #endif
}

void ReduceDB::sort_red_cls(ClauseClean clean_type)
{
    #ifdef VERBOSE_DEBUG
    cout << "Before sort" << endl;
    uint64_t i = 0;
    for(const auto& x: solver->longRedCls[2]) {
        const ClOffset offset = x;
        Clause* cl = solver->cl_alloc.ptr(offset);
        cout << i << " offset: " << offset << " cl->stats.last_touched: " << cl->stats.last_touched
        << " act:" << std::setprecision(9) << cl->stats.activity
        << " which_red_array:" << cl->stats.which_red_array << endl
        << " -- cl:" << *cl << " tern:" << cl->stats.is_ternary_resolvent
        << endl;
    }
    #endif

    switch (clean_type) {
        case ClauseClean::glue : {
            std::sort(solver->longRedCls[2].begin(), solver->longRedCls[2].end(), SortRedClsGlue(solver->cl_alloc));
            break;
        }

        case ClauseClean::activity : {
            std::sort(solver->longRedCls[2].begin(), solver->longRedCls[2].end(), SortRedClsAct(solver->cl_alloc));
            break;
        }

        default: {
            assert(false && "Unknown cleaning type");
        }
    }
}

#if defined(NORMAL_CL_USE_STATS)
void ReduceDB::gather_normal_cl_use_stats()
{
    for(uint32_t i = 0; i < 3; i++) {
        ClauseStats cl_stat;
        for(const auto& offset: solver->longRedCls[i]) {
            Clause* cl = solver->cl_alloc.ptr(offset);

            assert(cl->stats.introduced_at_conflict <= solver->sumConflicts);
            const uint64_t age = solver->sumConflicts - cl->stats.introduced_at_conflict;
            cl_stat.add_in(*cl, age);
            cl->stats.reset_rdb_stats();
        }
        cl_stat.print(i);
        cl_stats[i] += cl_stat;
    }
}

#endif

//TODO maybe we chould count binary learnt clauses as well into the
//kept no. of clauses as other solvers do
void ReduceDB::handle_lev2()
{
    solver->dump_memory_stats_to_sql();
    size_t orig_size = solver->longRedCls[2].size();

    const double myTime = cpuTime();
    assert(solver->watches.get_smudged_list().empty());

    //lev2 -- clean
    int64_t num_to_reduce = solver->longRedCls[2].size();
    for(unsigned keep_type = 0
        ; keep_type < sizeof(solver->conf.ratio_keep_clauses)/sizeof(double)
        ; keep_type++
    ) {
        const uint64_t keep_num = (double)num_to_reduce*solver->conf.ratio_keep_clauses[keep_type];
        if (keep_num == 0) {
            continue;
        }
        sort_red_cls(static_cast<ClauseClean>(keep_type));
        mark_top_N_clauses_lev2(keep_num);
    }
    assert(delayed_clause_free.empty());
    cl_marked = 0;
    cl_ttl = 0;
    cl_locked_solver = 0;
    remove_cl_from_lev2();

    solver->clean_occur_from_removed_clauses_only_smudged();
    for(ClOffset offset: delayed_clause_free) {
        solver->free_cl(offset);
    }
    delayed_clause_free.clear();

    #ifdef SLOW_DEBUG
    solver->check_no_removed_or_freed_cl_in_watch();
    #endif

    if (solver->conf.verbosity >= 2) {
        cout << "c [DBclean lev2]"
        << " confl: " << solver->sumConflicts
        << " orig size: " << orig_size
        << " marked: " << cl_marked
        << " ttl:" << cl_ttl
        << " locked_solver:" << cl_locked_solver
        << solver->conf.print_times(cpuTime()-myTime)
        << endl;
    }

    if (solver->sqlStats) {
        solver->sqlStats->time_passed_min(
            solver
            , "dbclean-lev2"
            , cpuTime()-myTime
        );
    }
    total_time += cpuTime()-myTime;

    last_reducedb_num_conflicts = solver->sumConflicts;
}

#if defined(FINAL_PREDICTOR) || defined(STATS_NEEDED)
const CMSat::ClauseStats&
ReduceDB::get_median_stat(const vector<ClOffset>& all_learnt) const
{
    return solver->cl_alloc.ptr(all_learnt[all_learnt.size()/2])->stats;
}

void ReduceDB::prepare_features(vector<ClOffset>& all_learnt)
{
    //Also save total_* data for stats
    std::sort(all_learnt.begin(), all_learnt.end(), SortRedClsProps(solver->cl_alloc));
    total_glue = 0;
    total_props = 0;
    total_uip1_used = 0;
    total_sum_uip1_used = 0;
    total_sum_props_used = 0;
    total_time_in_solver = 0;
    for(size_t i = 0; i < all_learnt.size(); i++) {
        ClOffset offs = all_learnt[i];
        Clause* cl = solver->cl_alloc.ptr(offs);
        ClauseStatsExtra& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];
        stats_extra.prop_ranking = i+1;
        assert(cl->stats.glue > 0);
        stats_extra.update_rdb_stats(cl->stats);

        total_glue += cl->stats.glue;
        total_props += cl->stats.props_made;
        total_uip1_used += cl->stats.uip1_used;
        total_sum_uip1_used += stats_extra.sum_uip1_used;
        total_sum_props_used += stats_extra.sum_props_made;
        assert(solver->sumConflicts >= stats_extra.introduced_at_conflict);
        total_time_in_solver += solver->sumConflicts - stats_extra.introduced_at_conflict;
    }
    if (all_learnt.empty()) {
        median_data.median_props = 0;
    } else {
        median_data.median_props = get_median_stat(all_learnt).props_made;
    }

    std::sort(all_learnt.begin(), all_learnt.end(), SortRedClsUIP1(solver->cl_alloc));
    for(size_t i = 0; i < all_learnt.size(); i++) {
        ClOffset offs = all_learnt[i];
        Clause* cl = solver->cl_alloc.ptr(offs);
        ClauseStatsExtra& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];
        stats_extra.uip1_ranking = i+1;
    }
    if (all_learnt.empty()) {
        median_data.median_uip1_used = 0;
    } else {
        median_data.median_uip1_used = get_median_stat(all_learnt).uip1_used;
    }

    // Sum UIP1 use/time
    std::sort(all_learnt.begin(), all_learnt.end(), SortRedClsSumUIPPerTime(solver));
    for(size_t i = 0; i < all_learnt.size(); i++) {
        ClOffset offs = all_learnt[i];
        Clause* cl = solver->cl_alloc.ptr(offs);
        ClauseStatsExtra& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];
        stats_extra.sum_uip1_per_time_ranking = i+1;
    }
    if (all_learnt.empty()) {
        median_data.median_sum_uip1_per_time = 0;
    } else {
        uint32_t extra_at = get_median_stat(all_learnt).extra_pos;
        ClauseStatsExtra& stats_extra = solver->red_stats_extra[extra_at];
        median_data.median_sum_uip1_per_time = stats_extra.calc_sum_uip1_per_time(solver->sumConflicts);
    }

    // Sum props/time
    std::sort(all_learnt.begin(), all_learnt.end(), SortRedClsSumPropsPerTime(solver));
    for(size_t i = 0; i < all_learnt.size(); i++) {
        ClOffset offs = all_learnt[i];
        Clause* cl = solver->cl_alloc.ptr(offs);
        ClauseStatsExtra& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];
        stats_extra.sum_props_per_time_ranking = i+1;
    }
    if (all_learnt.empty()) {
        median_data.median_sum_props_per_time = 0;
    } else {
        uint32_t extra_at = get_median_stat(all_learnt).extra_pos;
        ClauseStatsExtra& stats_extra = solver->red_stats_extra[extra_at];
        median_data.median_sum_props_per_time = stats_extra.calc_sum_props_per_time(solver->sumConflicts);
    }

    //We'll also compact solver->red_stats_extra
    uint32_t new_extra_pos = 0;
    vector<ClauseStatsExtra> new_red_stats_extra(all_learnt.size());
    std::sort(all_learnt.begin(), all_learnt.end(), SortRedClsAct(solver->cl_alloc));
    for(size_t i = 0; i < all_learnt.size(); i++) {
        ClOffset offs = all_learnt[i];
        Clause* cl = solver->cl_alloc.ptr(offs);
        ClauseStatsExtra& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];
        stats_extra.act_ranking = i+1;

        new_red_stats_extra[new_extra_pos] = stats_extra;
        cl->stats.extra_pos = new_extra_pos;
        new_extra_pos++;
    }
    if (all_learnt.empty()) {
        median_data.median_act = 0;
    } else {
        median_data.median_act = get_median_stat(all_learnt).activity;
    }
    std::swap(solver->red_stats_extra, new_red_stats_extra);
}
#endif

#ifdef STATS_NEEDED
void ReduceDB::dump_sql_cl_data(
    const uint32_t cur_rst_type
) {
    assert(solver->sqlStats);

    reduceDB_called++;
    double myTime = cpuTime();
    uint32_t non_locked_lev0 = 0;

    //Set up features
    vector<ClOffset> all_learnt;
    uint32_t num_locked_for_data_gen = 0;
    for(uint32_t lev = 0; lev < solver->longRedCls.size(); lev++) {
        auto& cc = solver->longRedCls[lev];
        for(const auto& offs: cc) {
            Clause* cl = solver->cl_alloc.ptr(offs);
            assert(!cl->getRemoved());
            assert(cl->red());
            assert(!cl->freed());
            if (cl->stats.locked_for_data_gen) {
                assert(cl->stats.which_red_array == 0);
            } else if (cl->stats.which_red_array == 0) {
                non_locked_lev0++;
            }
            all_learnt.push_back(offs);
            num_locked_for_data_gen += cl->stats.locked_for_data_gen;
        }
    }
    if (all_learnt.empty()) {
        return;
    }
    prepare_features(all_learnt);

    AverageCommonDataRDB avgdata;
    avgdata.avg_glue = (double)total_glue/(double)all_learnt.size();
    avgdata.avg_props = (double)total_props/(double)all_learnt.size();
    avgdata.avg_uip1_used = (double)total_uip1_used/(double)all_learnt.size();
    if (total_time_in_solver > 0) {
        avgdata.avg_sum_uip1_per_time = (double)total_sum_uip1_used/(double)(all_learnt.size()*total_time_in_solver);
        avgdata.avg_sum_props_per_time = (double)total_sum_props_used/(double)(all_learnt.size()*total_time_in_solver);
    }


    //Dump common features
    solver->sqlStats->begin_transaction();
    solver->sqlStats->reduceDB_common(
        solver,
        reduceDB_called,
        all_learnt.size(),
        cur_rst_type,
        median_data,
        avgdata
    );

    //Dump clause features
    uint64_t added_to_db = 0;
    for(size_t i = 0; i < all_learnt.size(); i++) {
        ClOffset offs = all_learnt[i];
        Clause* cl = solver->cl_alloc.ptr(offs);

        //Only if selected to be dumped
        if (cl->stats.ID == 0) {
            continue;
        }

        const bool locked = solver->clause_locked(*cl, offs);
        ClauseStatsExtra& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];
        solver->sqlStats->reduceDB(
            solver
            , locked
            , cl
            , reduceDB_called
        );
        added_to_db++;
        stats_extra.reset_rdb_stats(cl->stats);
    }
    solver->sqlStats->end_transaction();

    if (solver->conf.verbosity) {
        cout << "c [sql] added to DB " << added_to_db
        << " dump-ratio: " << solver->conf.dump_individual_cldata_ratio
        << " locked-perc: " << stats_line_percent(num_locked_for_data_gen, all_learnt.size())
        << " non-locked lev0: " << non_locked_lev0
        << solver->conf.print_times(cpuTime()-myTime)
        << endl;
    }
    locked_for_data_gen_total += num_locked_for_data_gen;
    locked_for_data_gen_cls += all_learnt.size();
}
#endif

void ReduceDB::handle_lev1()
{
    #ifdef VERBOSE_DEBUG
    cout << "c handle_lev1()" << endl;
    #endif

    uint32_t moved_w0 = 0;
    uint32_t used_recently = 0;
    uint32_t non_recent_use = 0;
    double myTime = cpuTime();
    size_t orig_size = solver->longRedCls[1].size();

    size_t j = 0;
    for(size_t i = 0
        ; i < solver->longRedCls[1].size()
        ; i++
    ) {
        const ClOffset offset = solver->longRedCls[1][i];
        Clause* cl = solver->cl_alloc.ptr(offset);
        #ifdef VERBOSE_DEBUG
        cout << "offset: " << offset << " cl->stats.last_touched: " << cl->stats.last_touched
        << " act:" << std::setprecision(9) << cl->stats.activity
        << " which_red_array:" << cl->stats.which_red_array << endl
        << " -- cl:" << *cl << " tern:" << cl->stats.is_ternary_resolvent
        << endl;
        #endif

        assert(!cl->stats.locked_for_data_gen);
        if (cl->stats.which_red_array == 0) {
            solver->longRedCls[0].push_back(offset);
            moved_w0++;
        } else if (cl->stats.which_red_array == 2) {
            assert(false && "we should never move up through any other means");
        } else {
            uint32_t must_touch = solver->conf.must_touch_lev1_within;
            if (cl->stats.is_ternary_resolvent) {
                must_touch *= solver->conf.ternary_keep_mult; //this multiplier is 6 by default
            }
            if (!solver->clause_locked(*cl, offset)
                && cl->stats.last_touched + must_touch < solver->sumConflicts
            ) {
                solver->longRedCls[2].push_back(offset);
                cl->stats.which_red_array = 2;

                //when stats are needed, activities are correctly updated
                //across all clauses
                //WARNING this changes the way things behave during STATS relative to non-STATS!
                #ifndef STATS_NEEDED
                cl->stats.activity = 0;
                solver->bump_cl_act<false>(cl);
                #endif
                non_recent_use++;
            } else {
                solver->longRedCls[1][j++] = offset;
                used_recently++;
            }
        }
    }
    solver->longRedCls[1].resize(j);

    if (solver->conf.verbosity >= 2) {
        cout << "c [DBclean lev1]"
        << " confl: " << solver->sumConflicts
        << " orig size: " << orig_size
        << " used recently: " << used_recently
        << " not used recently: " << non_recent_use
        << " moved w0: " << moved_w0
        << solver->conf.print_times(cpuTime()-myTime)
        << endl;
    }

    if (solver->sqlStats) {
        solver->sqlStats->time_passed_min(
            solver
            , "dbclean-lev1"
            , cpuTime()-myTime
        );
    }
    total_time += cpuTime()-myTime;
}

#ifdef FINAL_PREDICTOR
void ReduceDB::pred_move_to_lev1_and_lev0()
{
    //FOREVER
    uint32_t mark_forever = solver->conf.pred_forever_chunk;
    //mark_forever *= pow((double)solver->sumConflicts/10000.0, solver->conf.pred_forever_size_pow);

    std::sort(solver->longRedCls[2].begin(), solver->longRedCls[2].end(),
              SortRedClsPredForever(solver->cl_alloc, solver->red_stats_extra));
    size_t j = 0;
    for(uint32_t i = 0; i < solver->longRedCls[2].size(); i ++) {
        const ClOffset offset = solver->longRedCls[2][i];
        Clause* cl = solver->cl_alloc.ptr(offset);
        if (i < mark_forever) {
            moved_from_short_to_forever++;
            cl->stats.which_red_array = 0;
            solver->longRedCls[0].push_back(offset);
        } else {
            solver->longRedCls[2][j++] =solver->longRedCls[2][i];
        }
    }
    solver->longRedCls[2].resize(j);


    // LONG
    uint32_t mark_long = solver->conf.pred_long_chunk;
    //mark_long *= pow((double)solver->sumConflicts/10000.0, solver->conf.pred_forever_size_pow);

    std::sort(solver->longRedCls[2].begin(), solver->longRedCls[2].end(),
              SortRedClsPredLong(solver->cl_alloc, solver->red_stats_extra));

    j = 0;
    for(uint32_t i = 0; i < solver->longRedCls[2].size(); i ++) {
        const ClOffset offset = solver->longRedCls[2][i];
        Clause* cl = solver->cl_alloc.ptr(offset);
        if (i < mark_long) {
            moved_from_short_to_long++;
            cl->stats.which_red_array = 1;
            solver->longRedCls[1].push_back(offset);
        } else {
            solver->longRedCls[2][j++] =solver->longRedCls[2][i];
        }
    }
    solver->longRedCls[2].resize(j);
}

void ReduceDB::update_preds(const vector<ClOffset>& offs)
{
    vector<float> data(PRED_COLS*offs.size());

    uint32_t data_at = 0;
    for(size_t i = 0
        ; i < offs.size()
        ; i++
    ) {
        const ClOffset offset = offs[i];
        Clause* cl = solver->cl_alloc.ptr(offset);
        auto& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];

        double act_ranking_rel = safe_div(stats_extra.act_ranking, commdata.all_learnt_size);
        double uip1_ranking_rel = safe_div(stats_extra.uip1_ranking, commdata.all_learnt_size);
        double prop_ranking_rel = safe_div(stats_extra.prop_ranking, commdata.all_learnt_size);
        double sum_uip1_per_time_ranking_rel = safe_div(stats_extra.sum_uip1_per_time_ranking, commdata.all_learnt_size);
        double sum_props_per_time_ranking_rel = safe_div(stats_extra.sum_props_per_time_ranking, commdata.all_learnt_size);

        stats_extra.pred_short_use = 0;
        stats_extra.pred_long_use = 0;
        stats_extra.pred_forever_use = 0;
        assert(stats_extra.introduced_at_conflict <= solver->sumConflicts);
        uint64_t age = solver->sumConflicts - stats_extra.introduced_at_conflict;
        if (age > solver->conf.every_pred_reduce) {
            predictors->set_up_input(
                cl,
                solver->sumConflicts,
                act_ranking_rel,
                uip1_ranking_rel,
                prop_ranking_rel,
                sum_uip1_per_time_ranking_rel,
                sum_props_per_time_ranking_rel,
                commdata,
                PRED_COLS,
                solver,
                data.data()+data_at*PRED_COLS
            );
            data_at++;
        }
    }

    predictors->predict_all(data.data(), data_at);

    uint32_t retrieve_at = 0;
    for(const auto& offset: offs) {
        Clause* cl = solver->cl_alloc.ptr(offset);
        auto& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];

        assert(stats_extra.introduced_at_conflict <= solver->sumConflicts);
        uint64_t age = solver->sumConflicts - stats_extra.introduced_at_conflict;
        if (age > solver->conf.every_pred_reduce) {
            predictors->get_prediction_at(stats_extra, retrieve_at);
            retrieve_at++;

            if (solver->conf.pred_adjust_for_cl_size != 0) {
                double divby = safe_div(solver->stats.litsRedFinal, solver->stats.conflStats.numConflicts) * solver->conf.pred_adjust_for_cl_size;
                if (!solver->conf.pred_adjust_for_cl_size_onlyforever) {
                    stats_extra.pred_short_use = 1.0/(divby +
                        (double)cl->size())*stats_extra.pred_short_use;
                    stats_extra.pred_long_use = 1.0/(divby +
                        (double)cl->size())*stats_extra.pred_long_use;
                }
                stats_extra.pred_forever_use = 1.0/(divby +
                    (double)cl->size())*stats_extra.pred_forever_use;
            }
        }
    }

    predictors->finish_all_predict();
}

void ReduceDB::update_preds_lev2()
{
    double myTime = cpuTime();
    update_preds(solver->longRedCls[2]);
    dump_pred_distrib(solver->longRedCls[2], 2);

    if (solver->conf.verbosity >= 2) {
        double predTime = cpuTime() - myTime;
        cout << "c [DBCL] main predtime: " << predTime << endl;
    }
}

void ReduceDB::clean_lev0_once_in_a_while()
{
    //Deal with FOREVER once in a while
    if (num_times_pred_called % solver->conf.pred_forever_check_every_n !=
        (solver->conf.pred_forever_check_every_n-1)
    ) {
        return;
    }
    update_preds(solver->longRedCls[0]);
    dump_pred_distrib(solver->longRedCls[0], 0);

    //Clean up FOREVER, move to LONG
    const uint32_t checked_every = solver->conf.pred_forever_check_every_n *
        solver->conf.every_pred_reduce;

    uint32_t keep_forever = solver->conf.pred_forever_size;
    keep_forever *= pow((double)solver->sumConflicts/10000.0, solver->conf.pred_forever_size_pow);

    std::sort(solver->longRedCls[0].begin(), solver->longRedCls[0].end(),
          SortRedClsPredForever(solver->cl_alloc, solver->red_stats_extra));

    int j = 0;
    for(uint32_t i = 0; i < solver->longRedCls[0].size(); i ++) {
        const ClOffset offset = solver->longRedCls[0][i];
        Clause* cl = solver->cl_alloc.ptr(offset);
        auto& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];
        assert(!cl->freed());

        uint32_t time_inside_solver = solver->sumConflicts - stats_extra.introduced_at_conflict;
        if (i < keep_forever ||
            (time_inside_solver < checked_every/2 &&
            solver->conf.pred_dontmove_until_timeinside == 1) ||
            (time_inside_solver < checked_every &&
            solver->conf.pred_dontmove_until_timeinside == 2)
        ) {
//             if ((time_inside_solver < checked_every/2 &&
//                 solver->conf.pred_dontmove_until_timeinside == 1) ||
//                 (time_inside_solver < checked_every &&
//                 solver->conf.pred_dontmove_until_timeinside == 2))
//             {
//                 kept_in_forever_due_to_dontmove++;
//                 keep_forever++;
//             }

            //cout << "stats_extra.pred_forever_use: " << stats_extra.pred_forever_use/(10*1000.0) << endl;
            kept_in_forever++;
            assert(cl->stats.which_red_array == 0);
            solver->longRedCls[0][j++] = solver->longRedCls[0][i];
        } else {
            moved_from_forever_to_long++;
            solver->longRedCls[1].push_back(offset);
            cl->stats.which_red_array = 1;
        }
    }
    solver->longRedCls[0].resize(j);
}

void ReduceDB::clean_lev1_once_in_a_while()
{
    //Deal with LONG once in a while
    if (num_times_pred_called % solver->conf.pred_long_check_every_n !=
        (solver->conf.pred_long_check_every_n-1)
    ) {
        return;
    }

    update_preds(solver->longRedCls[1]);
    dump_pred_distrib(solver->longRedCls[1], 1);

    const uint32_t checked_every = solver->conf.pred_long_check_every_n * solver->conf.every_pred_reduce;


    //Clean up LONG
    std::sort(solver->longRedCls[1].begin(), solver->longRedCls[1].end(),
          SortRedClsPredLong(solver->cl_alloc, solver->red_stats_extra));
    uint32_t keep_long = solver->conf.pred_long_size;
    //keep_long *= pow((double)solver->sumConflicts/10000.0, solver->conf.pred_forever_size_pow);


    int j = 0;
    for(uint32_t i = 0; i < solver->longRedCls[1].size(); i ++) {
        const ClOffset offset = solver->longRedCls[1][i];
        Clause* cl = solver->cl_alloc.ptr(offset);
        auto& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];
        assert(!cl->freed());

        uint32_t time_inside_solver = solver->sumConflicts - stats_extra.introduced_at_conflict;
        if (i < keep_long ||
            (time_inside_solver < checked_every/2 &&
            solver->conf.pred_dontmove_until_timeinside == 1) ||
            (time_inside_solver < checked_every &&
            solver->conf.pred_dontmove_until_timeinside == 2)
        ) {
//             if ((time_inside_solver < checked_every/2 &&
//                 solver->conf.pred_dontmove_until_timeinside == 1) ||
//                 (time_inside_solver < checked_every &&
//                 solver->conf.pred_dontmove_until_timeinside == 2))
//             {
//                 kept_in_long_due_to_dontmove++;
//                 keep_long++;
//             }

            kept_in_long++;
            assert(cl->stats.which_red_array == 1);
            solver->longRedCls[1][j++] =solver->longRedCls[1][i];
        } else {
            moved_from_long_to_short++;
            solver->longRedCls[2].push_back(offset);
            cl->stats.which_red_array = 2;
        }
    }
    solver->longRedCls[1].resize(j);
}

void ReduceDB::delete_from_lev2()
{
    // SHORT
    uint32_t keep_short = solver->conf.pred_short_size;
    std::sort(solver->longRedCls[2].begin(), solver->longRedCls[2].end(),
              SortRedClsPredShort(solver->cl_alloc, solver->red_stats_extra));

    uint32_t j = 0;
    for(uint32_t i = 0; i < solver->longRedCls[2].size(); i ++) {
        const ClOffset offset = solver->longRedCls[2][i];
        Clause* cl = solver->cl_alloc.ptr(offset);
        auto& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];
        assert(!cl->freed());

        assert(stats_extra.introduced_at_conflict <= solver->sumConflicts);
        const uint64_t age = solver->sumConflicts - stats_extra.introduced_at_conflict;
        if (i < keep_short
            || solver->clause_locked(*cl, offset)
            || age < solver->conf.every_pred_reduce
        ) {
            if (solver->clause_locked(*cl, offset)
                || age < solver->conf.every_pred_reduce)
            {
                keep_short++;
                kept_in_short_due_to_dontmove++;
            }
            kept_in_short++;
            solver->longRedCls[2][j++] = solver->longRedCls[2][i];
        } else {
            short_deleted++;
            short_deleted_age += age;
            solver->watches.smudge((*cl)[0]);
            solver->watches.smudge((*cl)[1]);
            solver->litStats.redLits -= cl->size();

            *solver->drat << del << *cl << fin;
            cl->setRemoved();
            delayed_clause_free.push_back(offset);
        }
    }
    solver->longRedCls[2].resize(j);
}

ReduceDB::ClauseStats ReduceDB::reset_clause_dats(const uint32_t lev)
{
    ClauseStats cl_stat;
    uint32_t tot_age = 0;
    for(const auto& off: solver->longRedCls[lev]) {
        Clause* cl = solver->cl_alloc.ptr(off);
        auto& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];
        assert(!cl->freed());

        assert(stats_extra.introduced_at_conflict <= solver->sumConflicts);
        const uint64_t age = solver->sumConflicts - stats_extra.introduced_at_conflict;
        tot_age += age;
        cl_stat.add_in(*cl, age);
        stats_extra.reset_rdb_stats(cl->stats);
    }

    /*if (solver->conf.verbosity) {
        cout << "c [DBCL pred]"
        << " lev: " << lev
        << " avg age: " << std::fixed << std::setprecision(2) << std::setw(6)
        << ratio_for_stat(tot_age, solver->longRedCls[lev].size())
        << endl;
    }*/

    return cl_stat;
}

void ReduceDB::reset_predict_stats()
{
    ClauseStats this_stats;
    this_stats = reset_clause_dats(0);
    if (solver->conf.verbosity >= 2) {
        this_stats.print(0);
    }
    cl_stats[0] += this_stats;

    this_stats = reset_clause_dats(1);
    if (solver->conf.verbosity >= 2) {
        this_stats.print(1);
    }
    cl_stats[1] += this_stats;

    this_stats = reset_clause_dats(2);
    if (solver->conf.verbosity >= 2) {
        this_stats.print(2);
    }
    cl_stats[2] += this_stats;
}

void ReduceDB::dump_pred_distrib(const vector<ClOffset>& offs, uint32_t lev) {
    if (!solver->conf.dump_pred_distrib) {
        return;
    }
    std::ofstream distrib_file("pred_distrib.csv", ios::app);
    for(const auto& off:offs) {
        Clause* cl = solver->cl_alloc.ptr(off);
        ClauseStatsExtra& stats_extra = solver->red_stats_extra[cl->stats.extra_pos];
        const uint64_t age = solver->sumConflicts - stats_extra.introduced_at_conflict;
        if (age > solver->conf.every_pred_reduce)  {
            distrib_file
            << num_times_pred_called << ","
            << lev << "," << age
            << "," << stats_extra.pred_short_use
            << "," << stats_extra.pred_long_use
            << "," << stats_extra.pred_forever_use
            << endl;
        }
    }
}

void ReduceDB::handle_predictors()
{
    if (solver->conf.dump_pred_distrib && num_times_pred_called == 0) {
        std::ofstream distrib_file("pred_distrib.csv");
        distrib_file
        << "rdb_called" << ","
        << "tier" << "," << "age"
        << "," << "pred_short_use"
        << "," << "pred_long_use"
        << "," << "pred_forever_use"
        << endl;
    }
    num_times_pred_called++;
    if (predictors == NULL) {
        predictors = new ClPredictors;
        if (solver->conf.pred_conf_location.empty()) {
            predictors->load_models_from_buffers();
            if (solver->conf.verbosity) {
                cout << "c [pred] predictor hashes: ";
                for(const auto& h: predictors->get_hashes()) {
                    cout << h << " ";
                }
                cout << endl;
            }
        } else {
            vector<std::string> locations;
            locations.push_back(solver->conf.pred_conf_location + std::string("predictor_short.json"));
            locations.push_back(solver->conf.pred_conf_location + std::string("predictor_long.json"));
            locations.push_back(solver->conf.pred_conf_location + std::string("predictor_forever.json"));
            predictors->load_models(
                locations[0],
                locations[1],
                locations[2]);
            if (solver->conf.verbosity) {
                cout << "c [pred] loaded predictors from: ";
                for(const auto& l: locations) {
                    cout << l << " ";
                }
                cout << endl;
            }
        }
    }

    assert(delayed_clause_free.empty());
    double myTime = cpuTime();

    //Pre-reset, calculate common features
    vector<ClOffset> all_learnt;
    for(const auto& cls: solver->longRedCls) {
        for(const auto& offs: cls) {
            all_learnt.push_back(offs);
        }
    }
    prepare_features(all_learnt);
    commdata = ReduceCommonData(
        total_props, total_glue, total_uip1_used, total_sum_uip1_used,
        all_learnt.size(),
        median_data);

    //Move clauses around
    short_deleted = 0;
    moved_from_long_to_short = 0;
    kept_in_long = 0;
    kept_in_long_due_to_dontmove = 0;
    moved_from_forever_to_long = 0;
    kept_in_forever = 0;
    kept_in_forever_due_to_dontmove = 0;
    kept_in_short = 0;
    kept_in_short_due_to_dontmove = 0;
    short_deleted_age = 0;


    moved_from_short_to_forever = 0;
    moved_from_short_to_long = 0;

    update_preds_lev2();
    pred_move_to_lev1_and_lev0();
    delete_from_lev2();
    clean_lev0_once_in_a_while();
    clean_lev1_once_in_a_while();
    reset_predict_stats();

    //Cleanup
    solver->clean_occur_from_removed_clauses_only_smudged();
    for(ClOffset offset: delayed_clause_free) {
        solver->free_cl(offset);
    }
    delayed_clause_free.clear();

    //Stats
    if (solver->conf.verbosity >= 1) {
        cout
        << "c [DBCL pred]"
        << " short: " << print_value_kilo_mega(solver->longRedCls[2].size())
        << " long: "  << print_value_kilo_mega(solver->longRedCls[1].size())
        << " forever: "  << print_value_kilo_mega(solver->longRedCls[0].size())
        << endl;

        if (solver->conf.verbosity >= 1) {
            cout
            << "c [DBCL pred] lev0: " << std::setw(9) << solver->longRedCls[0].size()
            << " moved to lev1: " << std::setw(6) << moved_from_forever_to_long
            << " kept at lev0: " << std::setw(6) << kept_in_forever
            << " -- due to dontmove: " << std::setw(6) << kept_in_forever_due_to_dontmove
            << endl

            << "c [DBCL pred] lev1: " << std::setw(9) << solver->longRedCls[1].size()
            << " moved to lev2: " << std::setw(6) << moved_from_long_to_short
            << " kept at lev1: " << std::setw(6) << kept_in_long
            << " -- due to dontmove: " << std::setw(6) << kept_in_long_due_to_dontmove
            << endl

            << "c [DBCL pred] lev2: " << std::setw(9) << solver->longRedCls[2].size()
            << " m-to-lev1: " << std::setw(6) << moved_from_short_to_long
            << " m-to-lev0: " << std::setw(6) << moved_from_short_to_forever
            << " del: " << std::setw(6) << short_deleted
            << " kept: " << std::setw(6) << kept_in_short
            << " -- due to dontmove: " << std::setw(6) << kept_in_short_due_to_dontmove
            //<< " del avg age: " << std::setw(6) << safe_div(short_deleted_age, short_deleted)
            << endl;
        }

        cout
        << "c [DBCL pred] "
        << solver->conf.print_times(cpuTime()-myTime)
        << endl;
    }

    if (solver->sqlStats) {
        solver->sqlStats->time_passed_min(
            solver
            , "dbclean-lev1"
            , cpuTime()-myTime
        );
    }
    total_time += cpuTime()-myTime;
}
#endif

void ReduceDB::mark_top_N_clauses_lev2(const uint64_t keep_num)
{
    #ifdef VERBOSE_DEBUG
    cout << "Marking top N clauses " << keep_num << endl;
    #endif

    size_t marked = 0;
    for(size_t i = 0
        ; i < solver->longRedCls[2].size() && marked < keep_num
        ; i++
    ) {
        const ClOffset offset = solver->longRedCls[2][i];
        Clause* cl = solver->cl_alloc.ptr(offset);
        #ifdef VERBOSE_DEBUG
        cout << "offset: " << offset << " cl->stats.last_touched: " << cl->stats.last_touched
        << " act:" << std::setprecision(9) << cl->stats.activity
        << " which_red_array:" << cl->stats.which_red_array << endl
        << " -- cl:" << *cl << " tern:" << cl->stats.is_ternary_resolvent
        << endl;
        #endif

        if (cl->used_in_xor()
            || cl->stats.ttl > 0
            || solver->clause_locked(*cl, offset)
            || cl->stats.which_red_array != 2
        ) {
            //no need to mark, skip
            #ifdef VERBOSE_DEBUG
            cout << "Not marking Skipping "<< endl;
            #endif
            continue;
        }

        if (!cl->stats.marked_clause) {
            marked++;
            cl->stats.marked_clause = true;
            #ifdef VERBOSE_DEBUG
            cout << "Not marking Skipping "<< endl;
            #endif
        }
    }
}

bool ReduceDB::cl_needs_removal(const Clause* cl, const ClOffset offset) const
{
    assert(cl->red());
    return !cl->used_in_xor()
         && !cl->stats.marked_clause
         && cl->stats.ttl == 0
         && !solver->clause_locked(*cl, offset);
}

void ReduceDB::remove_cl_from_lev2() {
    size_t i, j;
    for (i = j = 0
        ; i < solver->longRedCls[2].size()
        ; i++
    ) {
        ClOffset offset = solver->longRedCls[2][i];
        Clause* cl = solver->cl_alloc.ptr(offset);
        assert(cl->size() > 2);

        //move to another array
        if (cl->stats.which_red_array < 2) {
            cl->stats.marked_clause = 0;
            solver->longRedCls[cl->stats.which_red_array].push_back(offset);
            continue;
        }
        assert(cl->stats.which_red_array == 2);

        //Check if locked, or marked or ttl-ed
        if (cl->stats.marked_clause) {
            cl_marked++;
        } else if (cl->stats.ttl != 0) {
            cl_ttl++;
        } else if (solver->clause_locked(*cl, offset)) {
            cl_locked_solver++;
        }

        if (!cl_needs_removal(cl, offset)) {
            if (cl->stats.ttl == 1) {
                cl->stats.ttl = 0;
            }
            solver->longRedCls[2][j++] = offset;
            cl->stats.marked_clause = 0;
            continue;
        }

        //Stats Update
        solver->watches.smudge((*cl)[0]);
        solver->watches.smudge((*cl)[1]);
        solver->litStats.redLits -= cl->size();

        *solver->drat << del << *cl << fin;
        cl->setRemoved();
        #ifdef VERBOSE_DEBUG
        cout << "REMOVING offset: " << offset << " cl->stats.last_touched: " << cl->stats.last_touched
        << " act:" << std::setprecision(9) << cl->stats.activity
        << " which_red_array:" << cl->stats.which_red_array << endl
        << " -- cl:" << *cl << " tern:" << cl->stats.is_ternary_resolvent
        << endl;
        #endif
        delayed_clause_free.push_back(offset);
    }
    solver->longRedCls[2].resize(j);
}

ReduceDB::ClauseStats ReduceDB::ClauseStats::operator += (const ClauseStats& other)
{
    total_uip1_used += other.total_uip1_used;
    total_props += other.total_props;
    total_cls += other.total_cls;
    total_age += other.total_age;

    return *this;
}

#if defined(NORMAL_CL_USE_STATS) || defined(STATS_NEEDED) || defined(FINAL_PREDICTOR)
void ReduceDB::ClauseStats::add_in(const Clause& cl, const uint64_t age)
{
    total_cls++;
    total_props += cl.stats.props_made;
    total_uip1_used += cl.stats.uip1_used;
    total_age += age;
}
#endif

void ReduceDB::ClauseStats::print(uint32_t lev)
{
    if (total_cls == 0) {
        return;
    }

    cout
    << "c [DBCL pred]"
    << " cl-stats " << lev << "]"
    << " (U+P)/cls: "
    << std::setw(7) << std::setprecision(4)
    << (double)(total_uip1_used)/(double)total_cls
    << " avg age: "
    << std::setw(7) << std::setprecision(1)
    << (double)(total_age)/((double)total_cls*1000) << "K"
    << endl;
}
