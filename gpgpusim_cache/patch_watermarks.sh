#!/bin/bash
sed -i '' '/m_pending_response_indices\[mf\] = cache_index;/a\
  if (block->get_pending_response_count() > m_max_line_refcount) {\
    m_max_line_refcount = block->get_pending_response_count();\
    if (m_max_line_refcount > g_abs_max_line_refcount) g_abs_max_line_refcount = m_max_line_refcount;\
  }\
  sample_queue_watermarks();\
' gpgpu_cache/gpu_cache_ref.cc

sed -i '' '/m_miss_queue.push_back/a\
  sample_queue_watermarks();\
' gpgpu_cache/gpu_cache_ref.cc

sed -i '' '/m_hit_response_queue.push_back/a\
  sample_queue_watermarks();\
' gpgpu_cache/gpu_cache_ref.cc

sed -i '' '/m_ready_response_queue.push_back/a\
  sample_queue_watermarks();\
' gpgpu_cache/gpu_cache_ref.cc

sed -i '' '/m_extra_mf_fields\[mf\] = /a\
  sample_queue_watermarks();\
' gpgpu_cache/gpu_cache_ref.cc

sed -i '' '/m_fragment_fifo.push/a\
  if (m_fragment_fifo.size() > g_abs_max_frag_fifo) g_abs_max_frag_fifo = m_fragment_fifo.size();\
' gpgpu_cache/gpu_cache_ref.cc

sed -i '' '/m_request_fifo.push/a\
  if (m_request_fifo.size() > g_abs_max_req_fifo) g_abs_max_req_fifo = m_request_fifo.size();\
' gpgpu_cache/gpu_cache_ref.cc

sed -i '' '/m_result_fifo.push/a\
  if (m_result_fifo.size() > g_abs_max_res_fifo) g_abs_max_res_fifo = m_result_fifo.size();\
' gpgpu_cache/gpu_cache_ref.cc

