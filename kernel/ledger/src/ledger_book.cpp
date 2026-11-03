#include "pixcake/ledger/ledger_book.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace pixcake {
namespace {

LedgerCache cache_from(const CreditLedger& ledger) {
  LedgerCache cache;
  cache.creative_points = ledger.creative_points();
  cache.export_sheets = ledger.export_sheets();
  cache.task_statuses = ledger.task_statuses();
  return cache;
}

bool same_cache(const LedgerCache& cache, const CreditLedger& ledger) {
  return cache.creative_points == ledger.creative_points() && cache.export_sheets == ledger.export_sheets() &&
         cache.task_statuses == ledger.task_statuses();
}

}  // namespace

LedgerBook::LedgerBook(LedgerEventLog& log, CreditLedger ledger) : log_(log), ledger_(std::move(ledger)) {}

ResultCode LedgerBook::open(FreeQueuePolicy policy, LedgerEventLog& log, std::unique_ptr<LedgerBook>& out) {
  out.reset();
  std::vector<LedgerEvent> events;
  const ResultCode loaded = log.load(events);
  if (loaded != ResultCode::Ok) {
    return loaded;
  }
  CreditLedger ledger = CreditLedger::open(std::move(policy), std::move(events));
  std::optional<LedgerCache> cache;
  const ResultCode read = log.read_cache(cache);
  if (read != ResultCode::Ok) {
    return read;
  }
  if (!cache.has_value() || !same_cache(*cache, ledger)) {
    const LedgerCache folded = cache_from(ledger);
    const ResultCode written = log.in_transaction([&] { return log.write_cache(folded); });
    if (written != ResultCode::Ok) {
      return written;
    }
  }
  out = std::unique_ptr<LedgerBook>(new LedgerBook(log, std::move(ledger)));
  return ResultCode::Ok;
}

ResultCode LedgerBook::persist(CreditLedger updated) {
  const std::size_t kept = ledger_.events().size();
  if (updated.events().size() < kept) {
    return ResultCode::StateConflict;
  }
  const std::vector<LedgerEvent> tail(updated.events().begin() + static_cast<std::ptrdiff_t>(kept),
                                      updated.events().end());
  const LedgerCache folded = cache_from(updated);
  const ResultCode code = log_.in_transaction([&] {
    const ResultCode appended = log_.append(tail);
    if (appended != ResultCode::Ok) {
      return appended;
    }
    return log_.write_cache(folded);
  });
  if (code != ResultCode::Ok) {
    return code;
  }
  ledger_ = std::move(updated);
  return ResultCode::Ok;
}

ResultCode LedgerBook::credit_points(std::int64_t amount) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.credit_points(amount); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

ResultCode LedgerBook::credit_sheets(std::int64_t amount) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.credit_sheets(amount); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

ResultCode LedgerBook::quote(const TaskQuote& quote) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.quote(quote); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

ResultCode LedgerBook::enqueue(const std::string& task_id) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.enqueue(task_id); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

ResultCode LedgerBook::start_generating(const std::string& task_id) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.start_generating(task_id); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

ResultCode LedgerBook::accelerate(const std::string& task_id) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.accelerate(task_id); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

ResultCode LedgerBook::cancel(const std::string& task_id) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.cancel(task_id); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

ResultCode LedgerBook::crash(const std::string& task_id) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.crash(task_id); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

ResultCode LedgerBook::succeed(const std::string& task_id) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.succeed(task_id); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

ResultCode LedgerBook::fail(const std::string& task_id) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.fail(task_id); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

ResultCode LedgerBook::stroke(const std::string& task_id, const std::string& stroke_id) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.stroke(task_id, stroke_id); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

ResultCode LedgerBook::settle_exports(const std::vector<ExportProduct>& products) {
  CreditLedger copy = ledger_;
  if (const ResultCode code = copy.settle_exports(products); code != ResultCode::Ok) {
    return code;
  }
  return persist(std::move(copy));
}

const CreditLedger& LedgerBook::ledger() const { return ledger_; }

}  // namespace pixcake
