package com.openrubi.luciatools.feature

import com.openrubi.luciatools.feature.StatScorer.Equip

/**
 * 一速(回溯)：六部位各取一件,不限套装,最大化「速度提升总和 + 速度套加成」。
 *  速度套(Speed) ≥4 件时给一次 +16%=pctBase×0.16(不叠加),不强制。
 *  最终=最优总和+(达4件?pctBase×0.16:0)+add。备注"样本:望月 · 速度套"(满足时显示后缀)。
 *  速度无百分比属性,仅 SpeedValue 固定值。
 */
object TopSpeed {

    const val SAMPLE_ROLE = "望月"
    const val SPEED_SET = "Speed"
    const val SET_THRESHOLD = 4
    const val SET_BONUS_RATE = 0.16

    private data class Cand(val v: Double, val isSet: Boolean)
    data class Sol(val value: Int, val note: String)

    private fun contribution(e: Equip): Double {
        var fixed = 0.0
        for ((t, vv) in e.allProps) if (t == "SpeedValue") fixed += vv
        return fixed
    }

    fun solve(r15: List<Equip>): Sol {
        val d = StatScorer.dim("speed")
        val byPart = LinkedHashMap<String, MutableList<Cand>>()
        for (p in StatScorer.PARTS) byPart[p] = ArrayList()
        for (e in r15) {
            if (e.part !in byPart) continue
            byPart[e.part]!!.add(Cand(contribution(e), e.set == SPEED_SET))
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
            return Sol((sum + d.add).toInt(), "样本:$SAMPLE_ROLE")
        }
        val bonus = d.pctBase * SET_BONUS_RATE
        val maxSuffix = DoubleArray(7)
        val setSuffix = IntArray(7)  // 后缀部位有速度套候选的数量(上界)
        for (i in 5 downTo 0) {
            maxSuffix[i] = maxSuffix[i + 1] + parts[i].maxOf { it.v }
            setSuffix[i] = setSuffix[i + 1] + if (parts[i].any { it.isSet }) 1 else 0
        }

        var bestVal = -1.0; var bestSet = 0
        fun dfs(idx: Int, sum: Double, setCnt: Int) {
            if (idx == 6) {
                val total = sum + if (setCnt >= SET_THRESHOLD) bonus else 0.0
                if (total > bestVal) { bestVal = total; bestSet = setCnt }
                return
            }
            // 上界:剩余全取最大速度 + (能否达4件套则可能有加成)
            val possibleSet = setCnt + setSuffix[idx]
            val upperBonus = if (possibleSet >= SET_THRESHOLD) bonus else 0.0
            if (sum + maxSuffix[idx] + upperBonus <= bestVal) return
            for (cand in parts[idx]) dfs(idx + 1, sum + cand.v, setCnt + if (cand.isSet) 1 else 0)
        }
        dfs(0, 0.0, 0)

        val note = if (bestSet >= SET_THRESHOLD) "样本:$SAMPLE_ROLE · 速度套" else "样本:$SAMPLE_ROLE"
        return Sol((bestVal + d.add).toInt(), note)
    }
}
