// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "include/pika_repl_client.h"

#include "include/pika_server.h"
#include "slash/include/slash_coding.h"
#include "slash/include/env.h"
#include "slash/include/slash_string.h"

extern PikaServer* g_pika_server;

PikaReplClient::PikaReplClient(int cron_interval, int keepalive_timeout) : next_avail_(0) {
  client_thread_ = new PikaReplClientThread(cron_interval, keepalive_timeout);
  client_thread_->set_thread_name("PikaReplClient");
  for (int i = 0; i < 2 * g_pika_conf->sync_thread_num(); ++i) {
    bg_workers_.push_back(new PikaReplBgWorker(PIKA_SYNC_BUFFER_SIZE));
  }
}

PikaReplClient::~PikaReplClient() {
  client_thread_->StopThread();
  delete client_thread_;
  for (size_t i = 0; i < bg_workers_.size(); ++i) {
    delete bg_workers_[i];
  }
  LOG(INFO) << "PikaReplClient exit!!!";
}

int PikaReplClient::Start() {
  int res = client_thread_->StartThread();
  if (res != pink::kSuccess) {
    LOG(FATAL) << "Start ReplClient ClientThread Error: " << res << (res == pink::kCreateThreadError ? ": create thread error " : ": other error");
  }
  for (size_t i = 0; i < bg_workers_.size(); ++i) {
    res = bg_workers_[i]->StartThread();
    if (res != pink::kSuccess) {
      LOG(FATAL) << "Start Pika Repl Worker Thread Error: " << res
          << (res == pink::kCreateThreadError ? ": create thread error " : ": other error");
    }
  }
  return res;
}

void PikaReplClient::Schedule(pink::TaskFunc func, void* arg) {
  bg_workers_[next_avail_]->Schedule(func, arg);
  UpdateNextAvail();
}

void PikaReplClient::ScheduleWriteBinlogTask(std::string table_partition,
    const std::shared_ptr<InnerMessage::InnerResponse> res,
    std::shared_ptr<pink::PbConn> conn, void* res_private_data) {
  size_t index = GetHashIndex(table_partition, true);
  ReplClientWriteBinlogTaskArg* task_arg =
    new ReplClientWriteBinlogTaskArg(res, conn, res_private_data, bg_workers_[index]);
  bg_workers_[index]->Schedule(&PikaReplBgWorker::HandleBGWorkerWriteBinlog, static_cast<void*>(task_arg));
}

void PikaReplClient::ScheduleWriteDBTask(const std::string& dispatch_key,
    PikaCmdArgsType* argv, BinlogItem* binlog_item,
    const std::string& table_name, uint32_t partition_id) {
  size_t index = GetHashIndex(dispatch_key, false);
  ReplClientWriteDBTaskArg* task_arg =
    new ReplClientWriteDBTaskArg(argv, binlog_item, table_name, partition_id);
  bg_workers_[index]->Schedule(&PikaReplBgWorker::HandleBGWorkerWriteDB, static_cast<void*>(task_arg));
}

size_t PikaReplClient::GetHashIndex(std::string key, bool upper_half) {
  size_t hash_base = bg_workers_.size() / 2;
  return (str_hash(key) % hash_base) + (upper_half ? 0 : hash_base);
}

Status PikaReplClient::Write(const std::string& ip, const int port, const std::string& msg) {
  return client_thread_->Write(ip, port, msg);
}

Status PikaReplClient::SendMetaSync() {
  InnerMessage::InnerRequest request;
  request.set_type(InnerMessage::kMetaSync);
  InnerMessage::InnerRequest::MetaSync* meta_sync = request.mutable_meta_sync();
  InnerMessage::Node* node = meta_sync->mutable_node();
  node->set_ip(g_pika_server->host());
  node->set_port(g_pika_server->port());

  std::string masterauth = g_pika_conf->masterauth();
  if (!masterauth.empty()) {
    meta_sync->set_auth(masterauth);
  }

  std::string to_send;
  std::string master_ip = g_pika_server->master_ip();
  int master_port = g_pika_server->master_port();
  if (!request.SerializeToString(&to_send)) {
    LOG(WARNING) << "Serialize Meta Sync Request Failed, to Master ("
      << master_ip << ":" << master_port << ")";
    return Status::Corruption("Serialize Failed");
  }

  LOG(INFO) << "Try Send Meta Sync Request to Master ("
    << master_ip << ":" << master_port << ")";
  return client_thread_->Write(master_ip, master_port + kPortShiftReplServer, to_send);
}

Status PikaReplClient::SendPartitionDBSync(const std::string& table_name,
                                           uint32_t partition_id,
                                           const BinlogOffset& boffset) {
  InnerMessage::InnerRequest request;
  request.set_type(InnerMessage::kDBSync);
  InnerMessage::InnerRequest::DBSync* db_sync = request.mutable_db_sync();
  InnerMessage::Node* node = db_sync->mutable_node();
  node->set_ip(g_pika_server->host());
  node->set_port(g_pika_server->port());
  InnerMessage::Partition* partition = db_sync->mutable_partition();
  partition->set_table_name(table_name);
  partition->set_partition_id(partition_id);

  InnerMessage::BinlogOffset* binlog_offset = db_sync->mutable_binlog_offset();
  binlog_offset->set_filenum(boffset.filenum);
  binlog_offset->set_offset(boffset.offset);

  std::string to_send;
  std::string master_ip = g_pika_server->master_ip();
  int master_port = g_pika_server->master_port();
  if (!request.SerializeToString(&to_send)) {
    LOG(WARNING) << "Serialize Partition DBSync Request Failed, to Master ("
      << master_ip << ":" << master_port << ")";
    return Status::Corruption("Serialize Failed");
  }
  return client_thread_->Write(master_ip, master_port + kPortShiftReplServer, to_send);
}


Status PikaReplClient::SendPartitionTrySync(const std::string& table_name,
                                            uint32_t partition_id,
                                            const BinlogOffset& boffset) {
  InnerMessage::InnerRequest request;
  request.set_type(InnerMessage::kTrySync);
  InnerMessage::InnerRequest::TrySync* try_sync = request.mutable_try_sync();
  InnerMessage::Node* node = try_sync->mutable_node();
  node->set_ip(g_pika_server->host());
  node->set_port(g_pika_server->port());
  InnerMessage::Partition* partition = try_sync->mutable_partition();
  partition->set_table_name(table_name);
  partition->set_partition_id(partition_id);

  InnerMessage::BinlogOffset* binlog_offset = try_sync->mutable_binlog_offset();
  binlog_offset->set_filenum(boffset.filenum);
  binlog_offset->set_offset(boffset.offset);

  std::string to_send;
  std::string master_ip = g_pika_server->master_ip();
  int master_port = g_pika_server->master_port();
  if (!request.SerializeToString(&to_send)) {
    LOG(WARNING) << "Serialize Partition TrySync Request Failed, to Master ("
      << master_ip << ":" << master_port << ")";
    return Status::Corruption("Serialize Failed");
  }
  return client_thread_->Write(master_ip, master_port + kPortShiftReplServer, to_send);
}

Status PikaReplClient::SendPartitionBinlogSyncAck(const std::string& table_name,
                                                  uint32_t partition_id,
                                                  const BinlogOffset& ack_start,
                                                  const BinlogOffset& ack_end) {
  InnerMessage::InnerRequest request;
  request.set_type(InnerMessage::kBinlogSync);
  InnerMessage::InnerRequest::BinlogSync* binlog_sync = request.mutable_binlog_sync();
  binlog_sync->set_table_name(table_name);
  binlog_sync->set_partition_id(partition_id);

  InnerMessage::BinlogOffset* ack_range_start = binlog_sync->mutable_ack_range_start();
  ack_range_start->set_filenum(ack_start.filenum);
  ack_range_start->set_offset(ack_start.offset);

  InnerMessage::BinlogOffset* ack_range_end = binlog_sync->mutable_ack_range_end();
  ack_range_end->set_filenum(ack_end.filenum);
  ack_range_end->set_offset(ack_end.offset);

  std::string to_send;
  std::string master_ip = g_pika_server->master_ip();
  int master_port = g_pika_server->master_port();
  if (!request.SerializeToString(&to_send)) {
    LOG(WARNING) << "Serialize Partition BinlogSync Request Failed, to Master ("
      << master_ip << ":" << master_port << ")";
    return Status::Corruption("Serialize Failed");
  }
  return client_thread_->Write(master_ip, master_port + kPortShiftReplServer, to_send);
}

void PikaReplClient::BuildBinlogPb(const RmNode& slave, const std::string& msg, uint32_t filenum, uint64_t offset, InnerMessage::InnerRequest* request) {
  //InnerMessage::InnerRequest::BinlogSync* binlog_msg = request->add_binlog_sync();
  //InnerMessage::Node* node = binlog_msg->mutable_node();
  //node->set_ip(slave.ip_);
  //node->set_port(slave.port_);
  //binlog_msg->set_table_name(slave.table_);
  //binlog_msg->set_partition_id(slave.partition_);
  //InnerMessage::BinlogOffset* binlog_offset = binlog_msg->mutable_binlog_offset();
  //binlog_offset->set_filenum(filenum);
  //binlog_offset->set_offset(offset);
  //binlog_msg->set_binlog(msg);
}