package com.openrubi.luciatools.feature

import com.openrubi.luciatools.feature.StatScorer.Equip

/**
 * 顶配攻击力(多约束回溯搜索)：
 *  从六部位各取一件(75级+15样本)，约束/优先级：
 *   1) 硬约束：≥4 件为暴伤套(Destruction)
 *   2) 优先级1：装备提供暴击率总和 ≥ 77%(主+副)
 *   3) 优先级2：在上前提下，暴击伤害总和尽量 → 150%(主+副)
 *   4) 优先级3：在上前提下，最大化攻击力提升(攻击维口径：项链只算副属性)
 *  最终值 = 该套攻击提升 + StatScorer.dim("attack").add(1228)
 *  备注：暴击率达77%→"100%暴击" 否则"(实际+23)%暴击"；
 *        暴伤达150%→"350暴伤" 否则"(实际+200)%暴伤"；样本角色固定"吉妮兹"。
 */
object TopAttack {

    const val SAMPLE_ROLE = "吉妮兹"
    const val DESTRUCTION_SET = "Destruction"
    const val MIN_DESTRUCTION = 4
    const val CRIT_TARGET = 77.0       // 装备需凑的暴击率
    const val CRIT_BASE = 23.0         // 角色基础暴击 → 77+23=100
    const val CDMG_TARGET = 150.0      // 装备需凑的暴伤
    const val CDMG_BASE = 200.0        // 角色基础暴伤 → 150+200=350

    data class Cand(
        val atk: Double, val crit: Double, val cdmg: Double,
        val isDestruction: Boolean, val sid: String, val part: String
    )

    data class Sol(val attackValue: Int, val note: String)

    private fun attackContribution(e: Equip): Double {
        val d = StatScorer.dim("attack")
        // 攻击维口径：项链只算副属性
        val props = if (e.part == "Necklace") e.subProps else e.allProps
        var fixed = 0.0; var rate = 0.0
        for ((t, v) in props) {
            if (t == "AttackValue") fixed += v
            if (t == "AttackRate") rate += v
        }
        return fixed + rate * d.pctBase
    }

    private fun critContribution(e: Equip): Double {
        var r = 0.0
        for ((t, v) in e.allProps) if (t == "CriticalRate") r += v
        return r * 100.0
    }

    private fun cdmgContribution(e: Equip): Double {
        var r = 0.0
        for ((t, v) in e.allProps) if (t == "CriticalDamageRate") r += v
        return r * 100.0
    }

    fun solve(r15: List<Equip>): Sol {
        val add = StatScorer.dim("attack").add
        // 按部位分候选(每部位限规模，按攻击力降序取前 N，加上暴伤套的前 N，避免组合爆炸)
        val byPart = LinkedHashMap<String, MutableList<Cand>>()
        for (p in StatScorer.PARTS) byPart[p] = ArrayList()
        for (e in r15) {
            if (e.part !in byPart) continue
            byPart[e.part]!!.add(
                Cand(attackContribution(e), critContribution(e), cdmgContribution(e),
                    e.set == DESTRUCTION_SET, e.staticId, e.part)
            )
        }
        // 每部位剪枝：保留 攻击Top12 ∪ 暴击Top8 ∪ 暴伤Top8 ∪ 暴伤套Top8
        val parts = StatScorer.PARTS.map { p ->
            val list = byPart[p]!!
            if (list.isEmpty()) return@map emptyList<Cand>()
            val keep = LinkedHashSet<Cand>()
            keep += list.sortedByDescending { it.atk }.take(12)
            keep += list.sortedByDescending { it.crit }.take(8)
            keep += list.sortedByDescending { it.cdmg }.take(8)
            keep += list.filter { it.isDestruction }.sortedByDescending { it.atk }.take(8)
            keep.toList()
        }
        if (parts.any { it.isEmpty() }) {
            // 某部位无装备，退化为简单最大攻击
            val atk = parts.filter { it.isNotEmpty() }.sumOf { it.maxOf { c -> c.atk } }
            return Sol((atk + add).toInt(), "样本:$SAMPLE_ROLE · 装备不足")
        }

        // 回溯：目标优先级 (critOK, cdmg, atk) 字典序最大；硬约束 destruction>=4
        var best: DoubleArray? = null   // [critReached(1/0), effectiveCdmg, atk, crit]
        val chosen = arrayOfNulls<Cand>(6)
        var bestChosen = arrayOfNulls<Cand>(6)

        fun better(a: DoubleArray, b: DoubleArray?): Boolean {
            if (b == null) return true
            for (i in a.indices) { if (a[i] > b[i] + 1e-9) return true; if (a[i] < b[i] - 1e-9) return false }
            return false
        }

        // 剩余部位可达的最大暴伤/攻击(上界，用于剪枝)
        val maxCdmgSuffix = DoubleArray(7); val maxAtkSuffix = DoubleArray(7)
        for (i in 5 downTo 0) {
            maxCdmgSuffix[i] = maxCdmgSuffix[i + 1] + parts[i].maxOf { it.cdmg }
            maxAtkSuffix[i] = maxAtkSuffix[i + 1] + parts[i].maxOf { it.atk }
        }

        fun dfs(idx: Int, destrCnt: Int, crit: Double, cdmg: Double, atk: Double) {
            if (idx == 6) {
                if (destrCnt < MIN_DESTRUCTION) return
                val critReached = if (crit >= CRIT_TARGET) 1.0 else 0.0
                val effCdmg = minOf(cdmg, CDMG_TARGET)
                val metric = doubleArrayOf(critReached, effCdmg, atk, crit)
                if (better(metric, best)) { best = metric; bestChosen = chosen.copyOf() }
                return
            }
            // 剪枝：当前已选 + 剩余部位(含当前)能放暴伤的数量 < 4 → 不可能满足
            if (destrCnt + countDestrParts(parts, idx) < MIN_DESTRUCTION) return
            for (cand in parts[idx]) {
                chosen[idx] = cand
                dfs(idx + 1, destrCnt + if (cand.isDestruction) 1 else 0,
                    crit + cand.crit, cdmg + cand.cdmg, atk + cand.atk)
            }
            chosen[idx] = null
        }

        dfs(0, 0, 0.0, 0.0, 0.0)

        val b = best ?: return Sol((maxAtkSuffix[0] + add).toInt(), "样本:$SAMPLE_ROLE · 无法满足约束")
        val finalAtk = (b[2] + add).toInt()
        val crit = b[3]; val cdmg = bestChosen.filterNotNull().sumOf { it.cdmg }
        val critNote = if (crit >= CRIT_TARGET) "100%暴击" else "${(crit + CRIT_BASE).toInt()}%暴击"
        val cdmgNote = if (cdmg >= CDMG_TARGET) "350暴伤" else "${(cdmg + CDMG_BASE).toInt()}%暴伤"
        return Sol(finalAtk, "样本:$SAMPLE_ROLE · $critNote · $cdmgNote · 暴伤套")
    }

    private fun canBeDestr(parts: List<List<Cand>>, idx: Int): Boolean =
        parts[idx].any { it.isDestruction }

    private fun countDestrParts(parts: List<List<Cand>>, from: Int): Int =
        (from until 6).count { canBeDestr(parts, it) }
}
