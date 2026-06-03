#!/bin/bash
sed -i '' '/m_list.push_back(mf);/a\
      if (m_data[mshr_idx].m_list.size() > m_max_merged_seen) m_max_merged_seen = m_data[mshr_idx].m_list.size();\
' gpgpu_cache/gpu_cache_ref.cc
