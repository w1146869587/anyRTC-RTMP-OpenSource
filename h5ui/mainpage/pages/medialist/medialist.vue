<template>
	<view class="index">
		<uni-list>
			<block v-for="(item, index) in lists" :key="index">
				<uni-list-item :title="item.RoomName" :note="item.CreateDate" show-extra-icon="true"  @click="goDetail(item)" ></uni-list-item>
			</block>
		</uni-list>		
		<text class="loadMore">{{loadMoreText}}</text>
	</view>
</template>

<script>
	
	import uniList from '../../components/uni-list/uni-list.vue'
	import uniListItem from '../../components/uni-list-item/uni-list-item.vue'	
    import {
        mapState,
        mapMutations
    } from 'vuex'
		
	export default {
		components: {
			uniList,
			uniListItem
		},		
		data() {
			return {
				refreshing: false,
				loadMoreText:"",
				lists: [],
				id: 0,
				fetchPageNum: 1,
			}
		},
		onLoad(e) {
			this.getroomid();
			this.id = e.id;
			setTimeout(() => { //防止app里由于渲染导致转场动画卡顿
				this.getData();
			}, 150)			

			uni.getProvider({
				service: "share",
				success: (e) => {
					let data = [];
					for (let i = 0; i < e.provider.length; i++) {
						switch (e.provider[i]) {
							case 'weixin':
								data.push({
									name: '分享到微信好友',
									id: 'weixin'
								})
								data.push({
									name: '分享到微信朋友圈',
									id: 'weixin',
									type: 'WXSenceTimeline'
								})
								break;
							case 'qq':
								data.push({
									name: '分享到QQ',
									id: 'qq'
								})
								break;
							default:
								break;
						}
					}
					this.providerList = data;
				},
				fail: (e) => {
					console.log("获取登录通道失败", e);
				}
			});
		},
		onPullDownRefresh() {
			console.log("下拉刷新");
			this.refreshing = true;
			this.getData();
		},
		onReachBottom() {
			console.log("上拉加载刷新");
			if(this.fetchPageNum > 20){
				this.loadMoreText = "没有更多了"
				return;
			}
			this.getData();
		},
		computed: mapState(['userInfo','roomid','hasLogin']),
		methods: {
			...mapMutations(['getroomid','setroomid']),
			getData(par) {
				const data = {
					action:"medialist",
					roomid:this.roomid,
				}
				let that = this;
				uni.request({
					url: this.$serverUrl + '/api/1.00/private',
					method: 'POST',
					data:data,
					dataType:'json',  
					header:{  
						'content-type':'application/json',
						'mster-token':this.userInfo.SessionId,
					}, 					
					success: (ret) => {
						if (ret.statusCode !== 200) {
							console.log("请求失败", ret)
						
						} else {
							if(ret.data.status != 0){
								uni.showToast({
									title:ret.data.msg,
								})
								return;
							}
							if (that.refreshing && ret.medialist[0].id === that.lists[0].id) {
								uni.showToast({
									title: "已经最新",
									icon: "none",
								})
								that.refreshing = false;
								uni.stopPullDownRefresh()
								return;
							}
							
							let lists = ret.data.medialist;
							console.log("list页面得到lists", lists);
							if (that.refreshing) {
								that.refreshing = false;
								uni.stopPullDownRefresh()
								that.lists = lists;
								that.fetchPageNum = 2;
								that.loadMoreText="下拉刷新";
							} else {
								that.lists = that.lists.concat(lists);
								that.fetchPageNum += 1;
							}
							
						}
					}
				});
			},
			goDetail(e) {
				//uni.navigateTo({
				//	url: "../detail/detail?data=" + encodeURIComponent(JSON.stringify(e))
				//})
				if(this.hasLogin == 1){
					const data ={
						cmd:"pulldlghis",
						data:e,
						ui:this.userInfo,
					}
					alert(JSON.stringify(data));
				}else{
					uni.showModal({
						title: '请先登录哦！',
						//content: '确定切换账户吗？',
						success: function (res) {
							if (res.confirm) {				
								
								uni.navigateTo({
									url:"../login/login"
								})
							} else if (res.cancel) {
								console.log('用户点击取消');
							}
						}
					});	
				}				
				
			},
			share(e) {
				if (this.providerList.length === 0) {
					uni.showModal({
						title: "当前环境无分享渠道!",
						showCancel: false
					})
					return;
				}
				let itemList = this.providerList.map(function (value) {
					return value.name
				})
				uni.showActionSheet({
					itemList: itemList,
					success: (res) => {
						uni.share({
							provider: this.providerList[res.tapIndex].id,
							scene: this.providerList[res.tapIndex].type && this.providerList[res.tapIndex].type === 'WXSenceTimeline' ?
								'WXSenceTimeline' : "WXSceneSession",
							type: 0,
							title: "uni-app模版",
							summary: e.title,
							imageUrl: e.img_src,
							href: "https://uniapp.dcloud.io",
							success: (res) => {
								console.log("success:" + JSON.stringify(res));
							},
							fail: (e) => {
								uni.showModal({
									content: e.errMsg,
									showCancel: false
								})
							}
						});
					}
				})
			}
		}
	}
</script>

<style>

</style>
