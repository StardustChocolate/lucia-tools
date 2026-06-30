package com.openrubi.luciatools.feature

import com.openrubi.luciatools.feature.StatScorer.Equip

/**
 * 顶配生命力(回溯搜索)：
 *  从六部位各取一件(75级+15样本)，不限套装种类，最大化「生命提升总和 + 生命套加成」。
 *  生命套(Health)每满 2 件 = 额外 +20% 生命词条 = pctBase×0.20，向下取整组数。
 *  最终值 = 最优总和 + 加成 + DimParam.add(6266.76)。
 *  备注：「样本:Aoi · 含N生命套」，N = 满足的"2件组"数。
 */
object TopHp {

    const val SAMPLE_ROLE = "Aoi"
    const val HEALTH_SET = "Health"
    const val SET_BONUS_RATE = 0.20    // 每2件生命套 +20%

    data class Cand(val hp: Double, val isHealth: Boolean)
    data class Sol(val hpValue: Int, val note: String)

    private fun hpContribution(e: Equip): Double {
        val d = StatScorer.dim("hp")
        // 生命维：主+副都算(项链也含主)
        var fixed = 0.0; var rate = 0.0
        for ((t, v) in e.allProps) {
            if (t == "HPValue") fixed += v
            if (t == "HPRate") rate += v
        }
        return fixed + rate * d.pctBase
    }

    fun solve(r15: List<Equip>): Sol {
        val d = StatScorer.dim("hp")
        val byPart = LinkedHashMap<String, MutableList<Cand>>()
        for (p in StatScorer.PARTS) byPart[p] = ArrayList()
        for (e in r15) {
            if (e.part !in byPart) continue
            byPart[e.part]!!.add(Cand(hpContribution(e), e.set == HEALTH_SET))
        }
        // 每部位剪枝：生命Top10(非生命套) ∪ 生命套Top10(按hp)，覆盖"高血"与"凑套"两类
        val parts = StatScorer.PARTS.map { p ->
            val list = byPart[p]!!
            if (list.isEmpty()) return@map emptyList<Cand>()
            val keep = LinkedHashSet<Cand>()
            keep += list.sortedByDescending { it.hp }.take(10)
            keep += list.filter { it.isHealth }.sortedByDescending { it.hp }.take(10)
            keep.toList()
        }
        if (parts.any { it.isEmpty() }) {
            // 退化：能取的部位直接取最大 hp
            val sum = parts.filter { it.isNotEmpty() }.sumOf { it.maxOf { c -> c.hp } }
            return Sol((sum + d.add).toInt(), "样本:$SAMPLE_ROLE · 装备不足")
        }

        val bonusPer = d.pctBase * SET_BONUS_RATE   // 每"2件组"的加成值

        // 后缀最大 hp(剪枝上界)
        val maxHpSuffix = DoubleArray(7)
        for (i in 5 downTo 0) maxHpSuffix[i] = maxHpSuffix[i + 1] + parts[i].maxOf { it.hp }

        var bestVal = -1.0; var bestHealth = 0
        fun dfs(idx: Int, hpSum: Double, healthCnt: Int) {
            if (idx == 6) {
                val groups = healthCnt / 2
                val total = hpSum + groups * bonusPer
                if (total > bestVal) { bestVal = total; bestHealth = healthCnt }
                return
            }
            // 上界剪枝：当前 + 剩余最大 hp + 满血套理论加成 仍不及最优则剪
            // (保守上界:剩余部位全为生命套 → (healthCnt+剩余件数)/2 组)
            val remain = 6 - idx
            val upperGroups = (healthCnt + remain) / 2
            if (hpSum + maxHpSuffix[idx] + upperGroups * bonusPer <= bestVal) return
            for (cand in parts[idx]) {
                dfs(idx + 1, hpSum + cand.hp, healthCnt + if (cand.isHealth) 1 else 0)
            }
        }
        dfs(0, 0.0, 0)

        val groups = bestHealth / 2
        val finalVal = (bestVal + d.add).toInt()
        return Sol(finalVal, "样本:$SAMPLE_ROLE · 含${groups}生命套")
    }
}
