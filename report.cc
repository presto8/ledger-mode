/*
 * Copyright (c) 2003-2008, John Wiegley.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of New Artisans LLC nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "report.h"
#include "reconcile.h"

namespace ledger {

xact_handler_ptr
report_t::chain_xact_handlers(xact_handler_ptr base_handler,
			      const bool handle_individual_xacts)
{
  bool remember_components = false;

  xact_handler_ptr handler(base_handler);

  // format_xacts write each xact received to the
  // output stream.
  if (handle_individual_xacts) {
    // truncate_entries cuts off a certain number of _entries_ from
    // being displayed.  It does not affect calculation.
    if (head_entries || tail_entries)
      handler.reset(new truncate_entries(handler, head_entries, tail_entries));

    // filter_xacts will only pass through xacts
    // matching the `display_predicate'.
    if (! display_predicate.empty())
      handler.reset(new filter_xacts(handler, display_predicate));

    // calc_xacts computes the running total.  When this
    // appears will determine, for example, whether filtered
    // xacts are included or excluded from the running total.
    handler.reset(new calc_xacts(handler));

    // component_xacts looks for reported xact that
    // match the given `descend_expr', and then reports the
    // xacts which made up the total for that reported
    // xact.
    if (! descend_expr.empty()) {
      std::list<std::string> descend_exprs;

      std::string::size_type beg = 0;
      for (std::string::size_type pos = descend_expr.find(';');
	   pos != std::string::npos;
	   beg = pos + 1, pos = descend_expr.find(';', beg))
	descend_exprs.push_back(std::string(descend_expr, beg, pos - beg));
      descend_exprs.push_back(std::string(descend_expr, beg));

      for (std::list<std::string>::reverse_iterator i =
	     descend_exprs.rbegin();
	   i != descend_exprs.rend();
	   i++)
	handler.reset(new component_xacts(handler, *i));

      remember_components = true;
    }

    // reconcile_xacts will pass through only those
    // xacts which can be reconciled to a given balance
    // (calculated against the xacts which it receives).
    if (! reconcile_balance.empty()) {
      datetime_t cutoff = current_moment;
      if (! reconcile_date.empty())
	cutoff = parse_datetime(reconcile_date);
      handler.reset(new reconcile_xacts
		    (handler, value_t(reconcile_balance), cutoff));
    }

    // filter_xacts will only pass through xacts
    // matching the `secondary_predicate'.
    if (! secondary_predicate.empty())
      handler.reset(new filter_xacts(handler, secondary_predicate));

    // sort_xacts will sort all the xacts it sees, based
    // on the `sort_order' value expression.
    if (! sort_string.empty()) {
      if (entry_sort)
	handler.reset(new sort_entries(handler, sort_string));
      else
	handler.reset(new sort_xacts(handler, sort_string));
    }

    // changed_value_xacts adds virtual xacts to the
    // list to account for changes in market value of commodities,
    // which otherwise would affect the running total unpredictably.
    if (show_revalued)
      handler.reset(new changed_value_xacts(handler, show_revalued_only));

    // collapse_xacts causes entries with multiple xacts
    // to appear as entries with a subtotaled xact for each
    // commodity used.
    if (show_collapsed)
      handler.reset(new collapse_xacts(handler));

    // subtotal_xacts combines all the xacts it receives
    // into one subtotal entry, which has one xact for each
    // commodity in each account.
    //
    // period_xacts is like subtotal_xacts, but it
    // subtotals according to time periods rather than totalling
    // everything.
    //
    // dow_xacts is like period_xacts, except that it
    // reports all the xacts that fall on each subsequent day
    // of the week.
    if (show_subtotal)
      handler.reset(new subtotal_xacts(handler, remember_components));

    if (days_of_the_week)
      handler.reset(new dow_xacts(handler, remember_components));
    else if (by_payee)
      handler.reset(new by_payee_xacts(handler, remember_components));

    // interval_xacts groups xacts together based on a
    // time period, such as weekly or monthly.
    if (! report_period.empty()) {
      handler.reset(new interval_xacts(handler, report_period,
					      remember_components));
      handler.reset(new sort_xacts(handler, "d"));
    }
  }

  // invert_xacts inverts the value of the xacts it
  // receives.
  if (show_inverted)
    handler.reset(new invert_xacts(handler));

  // related_xacts will pass along all xacts related
  // to the xact received.  If `show_all_related' is true,
  // then all the entry's xacts are passed; meaning that if
  // one xact of an entry is to be printed, all the
  // xact for that entry will be printed.
  if (show_related)
    handler.reset(new related_xacts(handler, show_all_related));

  // This filter_xacts will only pass through xacts
  // matching the `predicate'.
  if (! predicate.empty())
    handler.reset(new filter_xacts(handler, predicate));

#if 0
  // budget_xacts takes a set of xacts from a data
  // file and uses them to generate "budget xacts" which
  // balance against the reported xacts.
  //
  // forecast_xacts is a lot like budget_xacts, except
  // that it adds entries only for the future, and does not balance
  // them against anything but the future balance.

  if (budget_flags) {
    budget_xacts * budget_handler
      = new budget_xacts(handler, budget_flags);
    budget_handler->add_period_entries(journal->period_entries);
    handler.reset(budget_handler;

    // Apply this before the budget handler, so that only matching
    // xacts are calculated toward the budget.  The use of
    // filter_xacts above will further clean the results so
    // that no automated xacts that don't match the filter get
    // reported.
    if (! predicate.empty())
      handler.reset(new filter_xacts(handler, predicate));
  }
  else if (! forecast_limit.empty()) {
    forecast_xacts * forecast_handler
      = new forecast_xacts(handler, forecast_limit);
    forecast_handler->add_period_entries(journal->period_entries);
    handler.reset(forecast_handler;

    // See above, under budget_xacts.
    if (! predicate.empty())
      handler.reset(new filter_xacts(handler, predicate));
  }
#endif

  if (comm_as_payee)
    handler.reset(new set_comm_as_payee(handler));
  else if (code_as_payee)
    handler.reset(new set_code_as_payee(handler));

  return handler;
}

void report_t::xacts_report(xact_handler_ptr handler)
{
  session_xacts_iterator walker(session);
  pass_down_xacts(chain_xact_handlers(handler), walker);
  handler->flush();

  if (DO_VERIFY())
    session.clean_xacts();
}

void report_t::entry_report(xact_handler_ptr handler, entry_t& entry)
{
  entry_xacts_iterator walker(entry);
  pass_down_xacts(chain_xact_handlers(handler), walker);
  handler->flush();

  if (DO_VERIFY())
    session.clean_xacts(entry);
}

void report_t::sum_all_accounts()
{
  session_xacts_iterator walker(session);
  pass_down_xacts
    (chain_xact_handlers(xact_handler_ptr(new set_account_value), false),
     walker);
  // no flush() needed with set_account_value
  sum_accounts(*session.master);
}

void report_t::accounts_report(acct_handler_ptr handler,
			       const bool print_final_total)
{
  sum_all_accounts();

  if (sort_string.empty()) {
    accounts_iterator walker(*session.master);
    pass_down_accounts<accounts_iterator>(handler, walker);
  } else {
    sorted_accounts_iterator walker(*session.master, sort_string);
    pass_down_accounts<sorted_accounts_iterator>(handler, walker);
  }
  handler->flush();
    
  if (print_final_total && account_has_xdata(*session.master)) {
    account_xdata_t& xdata = account_xdata(*session.master);
    if (! show_collapsed && xdata.total) {
#if 0
      *out << "--------------------\n";
      xdata.value = xdata.total;
      handler->format.format(*out, details_t(*journal->master));
#endif
    }
  }

  if (DO_VERIFY()) {
    session.clean_xacts();
    session.clean_accounts();
  }
}

void report_t::commodities_report(const string& format)
{
}

void report_t::entry_report(const entry_t& entry, const string& format)
{
}

value_t report_t::abbrev(call_scope_t& args)
{
  if (args.size() < 2)
    throw_(std::logic_error, "usage: abbrev(STRING, WIDTH [, STYLE, ABBREV_LEN])");

  string str = args[0].as_string();
#if 0
  long	 wid = args[1];

  elision_style_t style = session.elision_style;
  if (args.size() == 3)
    style = static_cast<elision_style_t>(args[2].as_long());
#endif

  long abbrev_len = session.abbrev_length;
  if (args.size() == 4)
    abbrev_len = args[3].as_long();

#if 0
  return value_t(abbreviate(str, wid, style, true,
			    static_cast<int>(abbrev_len)), true);
#else
  return NULL_VALUE;
#endif
}

value_t report_t::ftime(call_scope_t& args)
{
  if (args.size() < 1)
    throw_(std::logic_error, "usage: ftime(DATE [, DATE_FORMAT])");

  datetime_t date = args[0].as_datetime();

  string date_format;
  if (args.size() == 2)
    date_format = args[1].as_string();
#if 0
  // jww (2007-04-18): Need to setup an output facet here
  else
    date_format = moment_t::output_format;

  return value_t(date.as_string(date_format), true);
#else
  return NULL_VALUE;
#endif
}

#if 0
optional<value_t>
report_t::resolve(const string& name, call_scope_t& args)
{
  const char * p = name.c_str();
  switch (*p) {
  case 'a':
    if (name == "abbrev") {
      return abbrev(args);
    }
    break;

  case 'f':
    if (name == "ftime") {
      return ftime(args);
    }
    break;
  }
  return scope_t::resolve(name, args);
}
#endif

expr_t::ptr_op_t report_t::lookup(const string& name)
{
  const char * p = name.c_str();
  switch (*p) {
  case 'o':
    if (std::strncmp(p, "option_", 7) == 0) {
      p = p + 7;
      switch (*p) {
      case 'a':
#if 0
	if (std::strcmp(p, "accounts") == 0)
	  return MAKE_FUNCTOR(report_t::option_accounts);
	else
#endif
	  if (std::strcmp(p, "amount") == 0)
	    return MAKE_FUNCTOR(report_t::option_amount);
	break;

      case 'b':
	if (std::strcmp(p, "bar") == 0)
	  return MAKE_FUNCTOR(report_t::option_bar);
	break;

#if 0
      case 'c':
	if (std::strcmp(p, "clean") == 0)
	  return MAKE_FUNCTOR(report_t::option_clean);
	else if (std::strcmp(p, "compact") == 0)
	  return MAKE_FUNCTOR(report_t::option_compact);
	break;
#endif

      case 'e':
#if 0
	if (std::strcmp(p, "entries") == 0)
	  return MAKE_FUNCTOR(report_t::option_entries);
	else if (std::strcmp(p, "eval") == 0)
	  return MAKE_FUNCTOR(report_t::option_eval);
	else if (std::strcmp(p, "exclude") == 0)
	  return MAKE_FUNCTOR(report_t::option_remove);
#endif
	break;

      case 'f':
#if 0
	if (std::strcmp(p, "foo") == 0)
	  return MAKE_FUNCTOR(report_t::option_foo);
	else
#endif
	  if (std::strcmp(p, "format") == 0)
	  return MAKE_FUNCTOR(report_t::option_format);
	break;

      case 'i':
#if 0
	if (std::strcmp(p, "include") == 0)
	  return MAKE_FUNCTOR(report_t::option_select);
#endif
	break;

      case 'l':
#if 0
	if (! *(p + 1) || std::strcmp(p, "limit") == 0)
	  return MAKE_FUNCTOR(report_t::option_limit);
#endif
	break;

#if 0
      case 'm':
	if (std::strcmp(p, "merge") == 0)
	  return MAKE_FUNCTOR(report_t::option_merge);
	break;
#endif

      case 'r':
#if 0
	if (std::strcmp(p, "remove") == 0)
	  return MAKE_FUNCTOR(report_t::option_remove);
#endif
	break;

#if 0
      case 's':
	if (std::strcmp(p, "select") == 0)
	  return MAKE_FUNCTOR(report_t::option_select);
	else if (std::strcmp(p, "split") == 0)
	  return MAKE_FUNCTOR(report_t::option_split);
	break;
#endif

      case 't':
	if (! *(p + 1))
	  return MAKE_FUNCTOR(report_t::option_amount);
	else if (std::strcmp(p, "total") == 0)
	  return MAKE_FUNCTOR(report_t::option_total);
	break;

      case 'T':
	if (! *(p + 1))
	  return MAKE_FUNCTOR(report_t::option_total);
	break;
      }
    }
    break;
  }

  return session.lookup(name);
}

} // namespace ledger
