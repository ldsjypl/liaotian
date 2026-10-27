package online.ldsjypl.chat;

import android.content.Context;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

/** 消息列表：自己 / 别人 / 系统提示 三种样式。 */
public class ChatAdapter extends BaseAdapter {

    public static final int TYPE_THEM = 0;
    public static final int TYPE_ME = 1;
    public static final int TYPE_SYS = 2;

    public static class Item {
        public int type;
        public String head = "";
        public String body = "";
    }

    private final List<Item> items = new ArrayList<>();

    public void add(Item item) {
        items.add(item);
        notifyDataSetChanged();
    }

    public void clear() {
        items.clear();
        notifyDataSetChanged();
    }

    @Override
    public int getCount() {
        return items.size();
    }

    @Override
    public Object getItem(int position) {
        return items.get(position);
    }

    @Override
    public long getItemId(int position) {
        return position;
    }

    @Override
    public View getView(int position, View convertView, ViewGroup parent) {
        View v = convertView;
        if (v == null) {
            Context ctx = parent.getContext();
            v = LayoutInflater.from(ctx).inflate(R.layout.item_message, parent, false);
        }
        Item it = items.get(position);
        LinearLayout root = (LinearLayout) v;
        TextView head = v.findViewById(R.id.itemHead);
        TextView body = v.findViewById(R.id.itemBody);

        body.setText(it.body);
        if (it.type == TYPE_SYS) {
            root.setGravity(Gravity.CENTER_HORIZONTAL);
            root.setPadding(0, dp(parent, 6), 0, dp(parent, 6));
            head.setVisibility(View.GONE);
            body.setBackgroundResource(R.drawable.bg_sys);
            body.setTextColor(0xFF8B949E);
            body.setTextSize(12);
        } else {
            boolean me = it.type == TYPE_ME;
            root.setGravity(me ? Gravity.END : Gravity.START);
            root.setPadding(0, dp(parent, 3), 0, dp(parent, 3));
            head.setVisibility(View.VISIBLE);
            head.setText(it.head);
            body.setBackgroundResource(me ? R.drawable.bg_bubble_me : R.drawable.bg_bubble_them);
            body.setTextColor(me ? 0xFFFFFFFF : 0xFFE6EDF3);
            body.setTextSize(15);
        }
        return v;
    }

    private static int dp(View v, int value) {
        return Math.round(value * v.getResources().getDisplayMetrics().density);
    }
}
