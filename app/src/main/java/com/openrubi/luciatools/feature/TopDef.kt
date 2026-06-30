package com.openrubi.luciatools.feature

import com.openrubi.luciatools.feature.StatScorer.Equip

/**
 * 顶配防御力(回溯,同顶配生命逻辑)：
 *  六部位各取一件,不限套装,最大化「防御提升总和 + 防御套加成」。
 *  防御套(Defense)每满2件=+20%=pctBase×0.20,组数向下取整。
 *  最终=最优总和+组数×(pctBase×0.20)+add。备注"样本:璀璨誓约的露比 · 含N防御套"。
 */
object TopDef {

    const val SAMPLE_ROLE = "璀璨誓约的露比"
    const val DEFENSE_SET = "Defense"
    const val SET_BONUS_RATE = 0.20

    private data class Cand(val v: Double, val isSet: Boolean)
    data class Sol(val value: Int, val note: String)

    private fun contribution(e: Equip): Double {
        val d = StatScorer.dim("defence")
        var fixed = 0.0; var rate = 0.0
        for ((t, vv) in e.allProps) {
            if (t == "DefenceValue") fixed += vv
            if (t == "DefenceRate") rate += vv
        }
        return fixed + rate * d.pctBase
    }

    fun solve(r15: List<Equip>): Sol {
        val d = StatScorer.dim("defence")
        val byPart = LinkedHashMap<String, MutableList<Cand>>()
        for (p in StatScorer.PARTS) byPart[p] = ArrayList()
        for (e in r15) {
            if (e.part !in byPart) continue
            byPart[e.part]!!.add(Cand(contribution(e), e.set == DEFENSE_SET))
        }
        val parts = StatScorer.PARTS.map { p ->
            val list = byPart[p]!!
            if (list.isEmpty()) return@map emptyList<Cand>()
            val keep = LinkedHashSet<Cand>()
            keep += list.sortedByDescending { it.v }.take(10)
            keep += list.filter { it.isSet }.sortedByDescending { it.v }.take(10)
            keep.toList()
        }
        if (parts.any { it.isEmpty() }) {
            val sum = parts.filter { it.isNotEmpty() }.sumOf { it.maxOf { c -> c.v } }
            return Sol((sum + d.add).toInt(), "样本:$SAMPLE_ROLE · 装备不足")
        }
        val bonusPer = d.pctBase * SET_BONUS_RATE
        val maxSuffix = DoubleArray(7)
        for (i in 5 downTo 0) maxSuffix[i] = maxSuffix[i + 1] + parts[i].maxOf { it.v }

        var bestVal = -1.0; var bestSet = 0
        fun dfs(idx: Int, sum: Double, setCnt: Int) {
            if (idx == 6) {
                val total = sum + (setCnt / 2) * bonusPer
                if (total > bestVal) { bestVal = total; bestSet = setCnt }
                return
            }
            val remain = 6 - idx
            val upperGroups = (setCnt + remain) / 2
            if (sum + maxSuffix[idx] + upperGroups * bonusPer <= bestVal) return
            for (cand in parts[idx]) dfs(idx + 1, sum + cand.v, setCnt + if (cand.isSet) 1 else 0)
        }
        dfs(0, 0.0, 0)

        return Sol((bestVal + d.add).toInt(), "样本:$SAMPLE_ROLE · 含${bestSet / 2}防御套")
    }
}
